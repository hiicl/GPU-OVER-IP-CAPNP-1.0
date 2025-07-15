// CUDA API Hook实现模块
// 功能：
//   1. 拦截CUDA运行时API调用
//   2. 将内存分配/内核启动等操作转发到远程GPU
//   3. 管理本地与远程GPU的内存映射关系
#include "hook_cuda.h"
#include "launcher_client.h"
#include "zmq_manager.h"
#include "context_manager.h"
#include "hook-launcher.capnp.h"
#include "cuda.capnp.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <kj/common.h>
#include <kj/debug.h>
#include <numa.h>  // 添加numa支持
#include <numaif.h> // 添加numa内存分配支持
#include "pch.h"

using json = nlohmann::json; // JSON解析库

// AlignData: 数据对齐函数
// 确保数据大小符合指定对齐要求
// 参数：
//   data - 需要对齐的数据向量
//   alignment - 对齐字节数
static void AlignData(std::vector<uint8_t>& data, size_t alignment) {
    if (alignment <= 1) return;
    
    const size_t current_size = data.size();
    const size_t padding = (alignment - (current_size % alignment)) % alignment;
    
    if (padding > 0) {
        data.insert(data.end(), padding, 0);
    }
}

// MemoryHandle: 内存句柄结构
// 管理本地与远程GPU内存映射关系
struct MemoryHandle {
    CUdeviceptr localPtr;
    uint64_t remoteHandle;
    int nodeId;
    bool isGpuMemory = false; // 标识是否为GPU内存
    int numaNode = -1; // 新增：内存所在的NUMA节点
};

// 管理Hook组件的核心资源
// 全局资源声明
static std::unique_ptr<LauncherClient> g_launcher_client;
static std::unique_ptr<ZmqManager> g_zmq_manager;
static std::unique_ptr<RdmaManager> g_rdma_manager; // RDMA管理器
static std::unique_ptr<kj::WaitScope> g_wait_scop
static std::map<CUfunction, std::string> g_function_name_map;
static MemoryMapper g_memory_mapper; // 内存映射管理器
static std::thread g_status_thread; // 状态监控线程

// 原始CUDA函数指针加载宏
#define LOAD_ORIG(func) pOriginal_##func = reinterpret_cast<func##_t>(GetProcAddress(cudaModule, #func))
typedef CUresult (CUDAAPI *cuMemAlloc_t)(CUdeviceptr*, size_t);
typedef CUresult (CUDAAPI *cuMemFree_t)(CUdeviceptr);
typedef CUresult (CUDAAPI *cuMemcpyHtoD_t)(CUdeviceptr, const void*, size_t);
typedef CUresult (CUDAAPI *cuMemcpyDtoH_t)(void*, CUdeviceptr, size_t);
typedef CUresult (CUDAAPI *cuLaunchKernel_t)(CUfunction, unsigned, unsigned, unsigned,
                                           unsigned, unsigned, unsigned,
                                           unsigned, CUstream, void**, void**);
typedef CUresult (CUDAAPI *cuModuleGetFunction_t)(CUfunction*, CUmodule, const char*);
// 新增高级功能hook函数类型
typedef CUresult (CUDAAPI *cuMemAdvise_t)(CUdeviceptr, size_t, CUmem_advise, CUdevice);
typedef CUresult (CUDAAPI *cuPointerGetAttribute_t)(void*, CUpointer_attribute, CUdeviceptr);
static HMODULE cudaModule = nullptr;
static cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
static cuMemFree_t pOriginal_cuMemFree = nullptr;
static cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
static cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
static cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;
static cuModuleGetFunction_t pOriginal_cuModuleGetFunction = nullptr;
// 新增高级功能原始函数指针
static cuMemAdvise_t pOriginal_cuMemAdvise = nullptr;
static cuPointerGetAttribute_t pOriginal_cuPointerGetAttribute = nullptr;

// InitOriginalFunctions: 加载原始CUDA函数
// 从nvcuda.dll获取原始函数指针用于后续调用
void InitOriginalFunctions() {
    cudaModule = LoadLibraryA("nvcuda.dll");
    if (!cudaModule) {
        std::cerr << "Failed to load nvcuda.dll" << std::endl;
        return;
    }
    LOAD_ORIG(cuMemAlloc);
    LOAD_ORIG(cuMemFree);
    LOAD_ORIG(cuMemcpyHtoD);
    LOAD_ORIG(cuMemcpyDtoH);
    LOAD_ORIG(cuLaunchKernel);
    LOAD_ORIG(cuModuleGetFunction);
}

// Hooked_cuModuleGetFunction: 拦截CUDA模块函数获取
// 建立CUfunction指针与函数名的映射关系
CUresult CUDAAPI Hooked_cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod, const char* name) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    CUresult res = pOriginal_cuModuleGetFunction(hfunc, hmod, name);
    if (res == CUDA_SUCCESS) {
        g_function_name_map[*hfunc] = name;
        std::cout << "[Hook] Mapped CUfunction " << *hfunc << " to name '" << name << "'" << std::endl;
    }
    return res;
}

// StatusMonitorThread: GPU状态监控线程
// 定期获取各节点GPU使用状态
void StatusMonitorThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 获取所有节点状态
        auto nodes = g_dispatcher.GetNodes();
        for (auto& node : nodes) {
            auto status = g_capnp_client->getGpuStatus(node.id);
            node.available_memory = status.getAvailableMemory();
            node.gpu_utilization = status.getUtilization();
        }
    }
}

// Hooked_cuMemAlloc: 拦截内存分配请求
CUresult CUDAAPI Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 请求分配计划（中心化决策）
    auto plan = g_launcher_client->requestAllocationPlanV2(byte_size);
    if (plan.error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Allocation failed: " << plan.error << std::endl;
        return CUDA_ERROR_OUT_OF_MEMORY;
    }
    
    // 获取当前NUMA节点（用于本地内存分配）
    int current_numa = numa_node_of_cpu(sched_getcpu());
    if (current_numa < 0) current_numa = 0;
    
    // 在本地NUMA节点分配主机内存
    void* host_ptr = numa_alloc_local(byte_size);
    if (!host_ptr) {
        std::cerr << "[Hook] NUMA allocation failed" << std::endl;
        return CUDA_ERROR_OUT_OF_MEMORY;
    }
    
    // 创建内存映射
    MemoryHandle handle;
    handle.localPtr = reinterpret_cast<CUdeviceptr>(host_ptr);
    handle.remoteHandle = plan.remoteHandle;
    handle.nodeId = plan.nodeId;
    handle.isGpuMemory = (plan.memoryType == MemoryType::VRAM);
    handle.numaNode = current_numa;
    handle.size = byte_size;
    
    // 保存映射关系
    *dev_ptr = handle.localPtr;
    g_memory_mapper.AddMapping(handle.localPtr, handle);
    
    std::cout << "[Hook] Allocated " << byte_size << " bytes on NUMA " << current_numa 
              << ", localPtr: 0x" << std::hex << handle.localPtr << std::dec
              << ", remoteHandle: " << handle.remoteHandle
              << ", nodeId: " << handle.nodeId 
              << ", memoryType: " << (handle.isGpuMemory ? "VRAM" : "HOST") 
              << ", transportType: " << static_cast<int>(plan.transportType) << std::endl;
    
    // 根据计划执行预取
    if (plan.prefetchHint) {
        g_launcher_client->prefetchData(handle.remoteHandle, byte_size);
    }
    
    return CUDA_SUCCESS;
}

// Hooked_cuMemFree: 拦截内存释放请求
// 将内存释放请求转发到远程GPU节点
// 参数：
//   dptr - 要释放的设备指针
// 返回值：CUDA错误码
CUresult CUDAAPI Hooked_cuMemFree(CUdeviceptr dptr) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 获取内存映射信息（使用MemoryHandle）
    auto* handle = g_memory_mapper.GetMapping(dptr);
    if (!handle) {
        return CUDA_ERROR_INVALID_VALUE;
    }
    
    // 释放本地NUMA内存
    if (handle->localPtr) {
        numa_free(reinterpret_cast<void*>(handle->localPtr), handle->size);
    }
    
    auto error = g_launcher_client->requestFreeV2(handle->remoteHandle, handle->nodeId);
    if (error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Free failed: " << error << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    // 移除内存映射
    g_memory_mapper.RemoveMapping(dptr);
    std::cout << "[Hook] Freed memory for fakePtr: 0x" 
              << std::hex << dptr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// Hooked_cuMemcpyHtoD: 拦截主机到设备内存复制
// 参数：
//   dstDevice - 目标设备地址
//   srcHost - 源主机地址
//   ByteCount - 复制字节数
// 返回值：CUDA错误码
CUresult CUDAAPI Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;
    
    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(dstDevice);
    if (!handle) {
        return pOriginal_cuMemcpyHtoD(dstDevice, srcHost, ByteCount);
    }

    // 确保线程在正确的NUMA节点运行
    if (handle->numaNode >= 0) {
        numa_run_on_node(handle->numaNode);
    }
    
    // 检测内存NUMA位置
    int src_numa = -1;
    get_mempolicy(&src_numa, NULL, 0, (void*)srcHost, MPOL_F_NODE | MPOL_F_ADDR);
    
    // 如果源内存不在本地NUMA节点，进行迁移
    if (src_numa >= 0 && handle->numaNode >= 0 && src_numa != handle->numaNode) {
        std::cout << "[Hook] Migrating memory from NUMA " << src_numa 
                  << " to NUMA " << handle->numaNode << std::endl;
                  
        void* migrated_ptr = numa_alloc_local(ByteCount);
        if (migrated_ptr) {
            memcpy(migrated_ptr, srcHost, ByteCount);
            srcHost = migrated_ptr;
            // 更新内存映射（如果需要）
        }
    }

    // 小数据传输直接使用RPC
    if (ByteCount < 1024) {
        MemcpyParams params;
        params.setDstPtr(reinterpret_cast<uint64_t>(dstDevice));
        params.setSrcPtr(reinterpret_cast<uint64_t>(srcHost));
        params.setSize(ByteCount);
        params.setKind(static_cast<uint32_t>(cudaMemcpyHostToDevice));
        
        auto result = g_launcher_client->requestMemcpyV2(params);
        if (result != CUDA_SUCCESS) {
            std::cerr << "[Hook] HtoD RPC transfer failed: " << result << std::endl;
            return CUDA_ERROR_UNKNOWN;
        }
        return CUDA_SUCCESS;
    }

    // 使用规划服务获取传输策略
    auto plan = g_launcher_client->requestPlanMemcpyHtoD(ByteCount);
    if (!plan.success) {
        std::cerr << "[Hook] PlanMemcpyHtoD failed" << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }

    // 根据传输策略选择执行路径
    switch (plan.transportType) {
        case TransportType::RDMA:
            if (g_rdma_manager && plan.rdma_endpoint) {
                RdmaEndpoint endpoint = plan.rdma_endpoint;
                if (g_rdma_manager->Transfer(const_cast<void*>(srcHost), ByteCount,
                                             endpoint.remote_addr, endpoint.rkey)) {
                    return CUDA_SUCCESS;
                } else {
                    std::cerr << "[Hook] RDMA transfer failed" << std::endl;
                }
            }
            break;
            
        case TransportType::UDP:
            if (g_zmq_manager) {
                if (g_zmq_manager->Transfer(plan.target_ip, plan.port, 
                                            const_cast<void*>(srcHost), ByteCount)) {
                    return CUDA_SUCCESS;
                } else {
                    std::cerr << "[Hook] UDP transfer failed" << std::endl;
                }
            }
            break;
            
        case TransportType::TCP:
            // 小数据传输直接使用RPC（TCP）
            MemcpyParams params;
            params.setDstPtr(reinterpret_cast<uint64_t>(dstDevice));
            params.setSrcPtr(reinterpret_cast<uint64_t>(srcHost));
            params.setSize(ByteCount);
            params.setKind(static_cast<uint32_t>(cudaMemcpyHostToDevice));
            
            auto result = g_launcher_client->requestMemcpyV2(params);
            if (result == CUDA_SUCCESS) {
                return CUDA_SUCCESS;
            } else {
                std::cerr << "[Hook] TCP transfer failed: " << result << std::endl;
            }
            break;
    }
    
    // 所有传输方式都失败，回退到原始实现
    return pOriginal_cuMemcpyHtoD(dstDevice, srcHost, ByteCount);
}

// Hooked_cuMemcpyDtoH: 拦截设备到主机内存复制
// 参数：
//   dstHost - 目标主机地址
//   srcDevice - 源设备地址
//   ByteCount - 复制字节数
// 返回值：CUDA错误码
CUresult CUDAAPI Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(srcDevice);
    if (!handle) {
        return pOriginal_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
    }
    
    // 确保线程在正确的NUMA节点运行
    if (handle->numaNode >= 0) {
        numa_run_on_node(handle->numaNode);
    }
    
    // 使用规划服务获取传输策略
    auto plan = g_launcher_client->requestPlanMemcpyDtoH(ByteCount);
    if (!plan.success) {
        std::cerr << "[Hook] PlanMemcpyDtoH failed" << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }

    // 根据传输策略选择执行路径
    switch (plan.transportType) {
        case TransportType::RDMA:
            if (g_rdma_manager && plan.rdma_endpoint) {
                RdmaEndpoint endpoint = plan.rdma_endpoint;
                if (g_rdma_manager->Receive(dstHost, ByteCount,
                                            endpoint.remote_addr, endpoint.rkey)) {
                    return CUDA_SUCCESS;
                } else {
                    std::cerr << "[Hook] RDMA transfer failed" << std::endl;
                }
            }
            break;
            
        case TransportType::UDP:
            if (g_zmq_manager) {
                if (g_zmq_manager->Receive(plan.target_ip, plan.port, 
                                           dstHost, ByteCount)) {
                    return CUDA_SUCCESS;
                } else {
                    std::cerr << "[Hook] UDP transfer failed" << std::endl;
                }
            }
            break;
            
        case TransportType::TCP:
            MemcpyParams params;
            params.setSrcPtr(reinterpret_cast<uint64_t>(srcDevice));
            params.setDstPtr(reinterpret_cast<uint64_t>(dstHost));
            params.setSize(ByteCount);
            params.setKind(static_cast<uint32_t>(cudaMemcpyDeviceToHost));
            
            auto result = g_launcher_client->requestMemcpyV2(params);
            if (result == CUDA_SUCCESS) {
                return CUDA_SUCCESS;
            } else {
                std::cerr << "[Hook] TCP transfer failed: " << result << std::endl;
            }
            break;
    }
    
    // 所有传输方式都失败，回退到原始实现
    return pOriginal_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
}

// Hooked_cuLaunchKernel: 拦截内核启动请求
// 核心功能：
//   1. 转换内核参数为跨节点协议
//   2. 处理参数对齐要求
//   3. 通过RPC发送到远程执行
// 参数：标准cuLaunchKernel参数
// 返回值：CUDA错误码
CUresult CUDAAPI Hooked_cuLaunchKernel(
    CUfunction f,
    unsigned gridDimX, unsigned gridDimY, unsigned gridDimZ,
    unsigned blockDimX, unsigned blockDimY, unsigned blockDimZ,
    unsigned sharedMemBytes, CUstream hStream,
    void** kernelParams, void** extra) {

    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_LAUNCH_FAILED;

    // 查找内核名称
    auto it = g_function_name_map.find(f);
    if (it == g_function_name_map.end()) {
        std::cerr << "[Hook] Unknown kernel function" << std::endl;
        return CUDA_ERROR_INVALID_HANDLE;
    }
    const std::string& kernelName = it->second;
    
    // 获取内存映射信息
    auto* allocInfo = g_memory_mapper.GetMapping(reinterpret_cast<CUdeviceptr>(f));
    if (!allocInfo) {
        std::cerr << "[Hook] Kernel function not mapped" << std::endl;
        return CUDA_ERROR_INVALID_HANDLE;
    }

    // 使用ProtocolAdapter转换参数
    auto launch = ProtocolAdapter::convertToKernelLaunch(
        kernelName,
        gridDimX, gridDimY, gridDimZ,
        blockDimX, blockDimY, blockDimZ,
        sharedMemBytes,
        kernelParams
    );
    
    // 计算参数总大小
    size_t paramBytes = 0;
    for (const auto& param : launch.getParams()) {
        paramBytes += param.getValue().size();
    }

    try {
        auto req = g_launcher_client->getClient().launchKernelV2Request();
        req.setKernelName(launch.getKernelName());
        req.setGridDimX(launch.getGridDimX());
        req.setGridDimY(launch.getGridDimY());
        req.setGridDimZ(launch.getGridDimZ());
        req.setBlockDimX(launch.getBlockDimX());
        req.setBlockDimY(launch.getBlockDimY());
        req.setBlockDimZ(launch.getBlockDimZ());
        req.setSharedMemBytes(launch.getSharedMemBytes());
        req.setStreamHandle(launch.getStreamHandle());  // 添加流句柄
        req.setParamBytes(paramBytes); // 设置参数总大小
        req.setFuncHandle(0); // 函数句柄（客户端暂未实现）
        req.setLaunchFlags(0); // 启动标志（保留字段）
        
        // 设置参数列表
        auto paramsBuilder = req.initParams(launch.getParams().size());
        for (int i = 0; i < launch.getParams().size(); i++) {
    auto paramBuilder = paramsBuilder[i];
    auto& param = launch.getParams()[i];
    
    // 获取对齐值（默认为4）
    uint32_t alignment = param.getAlignment();
    if (alignment == 0) alignment = 4;
    
    // 复制原始数据
    auto value = param.getValue();
    std::vector<uint8_t> alignedData(value.begin(), value.end());
    
    // 应用对齐填充
    AlignData(alignedData, alignment);
    
    paramBuilder.setType(param.getType());
    paramBuilder.setValue(kj::ArrayPtr<const uint8_t>(alignedData.data(), alignedData.size()));
    paramBuilder.setOffset(0); // 缓冲区偏移（暂未使用）
    paramBuilder.setAlignment(alignment);
        }

        auto response = req.send().wait(*g_wait_scope);
        auto err = response.getError();
        return static_cast<CUresult>(err);

    } catch (const kj::Exception& e) {
        std::cerr << "[Hook] Exception in LaunchKernel: " << e.getDescription().cStr() << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
}

// 模块初始化与清理
// 管理Hook组件的生命周期
// InitializeHook: 初始化Hook组件
// 功能：
//   1. 加载原始CUDA函数
//   2. 连接Launcher服务
//   3. 初始化ZMQ通信
//   4. 启动状态监控线程
void InitializeHook() {
    // 初始化原始函数
    InitOriginalFunctions();
    
    // 创建Launcher客户端
    g_launcher_client = std::make_unique<LauncherClient>();
    if (!g_launcher_client->connect("127.0.0.1:12345")) {
        std::cerr << "[Hook] Failed to connect to launcher" << std::endl;
    }
    
    // 初始化ZMQ管理器
    g_zmq_manager = std::make_unique<ZmqManager>();
    if (!g_zmq_manager->Initialize()) {
        std::cerr << "[Hook] ZMQ initialization failed" << std::endl;
    }
    
    // 初始化RDMA管理器
    g_rdma_manager = std::make_unique<RdmaManager>();
    int numa_node = numa_node_of_cpu(sched_getcpu());
    if (numa_node >= 0) {
        if (!g_rdma_manager->Initialize(numa_node)) {
            std::cerr << "[Hook] RDMA initialization failed" << std::endl;
            g_rdma_manager.reset();
        } else {
            std::cout << "[Hook] RDMA initialized for NUMA node " << numa_node << std::endl;
        }
    } else {
        std::cerr << "[Hook] Failed to determine NUMA node" << std::endl;
        g_rdma_manager.reset();
    }
    
    // 启动状态监控线程
    g_status_thread = std::thread(StatusMonitorThread);
    
    // 删除重复的初始化代码

    // ===== 设置新hook函数 =====
    // 安装cuMemAdvise和cuPointerGetAttribute的hook
    pOriginal_cuMemAdvise = hookFunction("cuMemAdvise", Hooked_cuMemAdvise);
    pOriginal_cuPointerGetAttribute = hookFunction("cuPointerGetAttribute", Hooked_cuPointerGetAttribute);
    
    std::cout << "[Hook] Initialized" << std::endl;
}

// CleanupHook: 清理Hook资源
// 释放所有全局资源并断开连接
void CleanupHook() {
    // 停止状态监控线程
    if (g_status_thread.joinable()) {
        g_status_thread.join();
    }
    
    // 清理资源
    g_zmq_manager.reset();
    g_launcher_client.reset();
    
    if (cudaModule) {
        FreeLibrary(cudaModule);
        cudaModule = nullptr;
    }
    
    std::cout << "[Hook] Cleaned up" << std::endl;
    std::lock_guard<std::mutex> lock(g_api_mutex);
    g_zmq_manager.reset();
    g_launcher_client.reset();
    g_wait_scope.reset();
    if (cudaModule) { FreeLibrary(cudaModule); cudaModule = nullptr; }
    std::cout << "[Hook] Cleaned up" << std::endl;
}

// ==== 新增高级功能hook实现 ====

CUresult CUDAAPI Hooked_cuMemAdvise(CUdeviceptr devPtr, size_t count, CUmem_advise advice, CUdevice device) {
    // 只有当建议是预取时，我们才转发它
    if (advice == CU_MEM_ADVISE_SET_PREFERRED_LOCATION || advice == CU_MEM_ADVISE_SET_ACCESSED_BY) {
        std::lock_guard<std::mutex> lock(g_api_mutex);
        if (!g_launcher_client || !g_launcher_client->isConnected()) {
            // 如果未连接，可以选择静默失败或调用原始函数
            return pOriginal_cuMemAdvise(devPtr, count, advice, device);
        }

        try {
            std::cout << "[Hook] Forwarding prefetch advice for fakePtr: 0x" 
                      << std::hex << devPtr << std::dec << std::endl;
            g_launcher_client->advisePrefetch(devPtr);
        } catch (const std::exception& e) {
            std::cerr << "[Hook] Exception in cuMemAdvise: " << e.what() << std::endl;
            // 不要因为建议失败而让应用失败
        }
    }

    // 对于所有建议，都调用原始函数，以确保本地驱动状态一致
    return pOriginal_cuMemAdvise(devPtr, count, advice, device);
}

CUresult CUDAAPI Hooked_cuPointerGetAttribute(void* data, CUpointer_attribute attribute, CUdeviceptr ptr) {
    // 我们可以拦截对设备ID的查询，并从launcher获取真实信息
    if (attribute == CU_POINTER_ATTRIBUTE_DEVICE_ORDINAL) {
        std::lock_guard<std::mutex> lock(g_api_mutex);
        if (!g_launcher_client || !g_launcher_client->isConnected()) 
            return CUDA_ERROR_LAUNCH_FAILED;

        try {
            auto location = g_launcher_client->getMemoryLocation(ptr);
            if (location.getError() == CUDA_SUCCESS) {
                // 将获取到的远程设备ID写入到输出参数
                *(static_cast<int*>(data)) = location.getNodeId();
                std::cout << "[Hook] Got remote device location for fakePtr 0x" << std::hex << ptr
                          << ": Node " << location.getNodeId() << std::dec << std::endl;
                return CUDA_SUCCESS;
            } else {
                return static_cast<CUresult>(location.getError());
            }
        } catch (const std::exception& e) {
            std::cerr << "[Hook] Exception in cuPointerGetAttribute: " << e.what() << std::endl;
            return CUDA_ERROR_LAUNCH_FAILED;
        }
    }

    // 对于其他所有属性，调用原始函数
    return pOriginal_cuPointerGetAttribute(data, attribute, ptr);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: InitializeHook(); break;
    case DLL_PROCESS_DETACH: CleanupHook(); break;
    }
    return TRUE;
}
