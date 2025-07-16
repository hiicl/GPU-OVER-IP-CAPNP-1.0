// CUDA API Hook实现模块
#include "hook_cuda.h"
#include "hook-launcher.capnp.h"
#include "cuda.capnp.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <kj/common.h>
#include <kj/debug.h>
#include <numa.h>
#include <numaif.h>
#include "pch.h"
#include "cooling_service.h"
#include "memory_mapper.h"
#include "services/memory_service.h"
#include "services/transport_service.h"
#include "services/advise_service.h"

using json = nlohmann::json;

// 对齐函数
static void AlignData(std::vector<uint8_t>& data, size_t alignment) {
    if (alignment <= 1) return;
    const size_t current_size = data.size();
    const size_t padding = (alignment - (current_size % alignment)) % alignment;
    if (padding > 0) {
        data.insert(data.end(), padding, 0);
    }
}

// 内存句柄结构
struct MemoryHandle {
    CUdeviceptr localPtr;
    uint64_t remoteHandle;
    int nodeId;
    bool isGpuMemory = false;
    int numaNode = -1;
    size_t size = 0;
};

// 全局资源声明
static std::unique_ptr<kj::WaitScope> g_wait_scope;
static std::map<CUfunction, std::string> g_function_name_map;
static MemoryMapper g_memory_mapper;
static std::thread g_status_thread;

// 服务实例
static MemoryService g_memory_service;
static TransportService g_transport_service;
static AdviseService g_advise_service(CoolingService::Instance());

// 原始函数指针声明
#define LOAD_ORIG(func) pOriginal_##func = reinterpret_cast<func##_t>(GetProcAddress(cudaModule, #func))
typedef CUresult (CUDAAPI *cuMemAlloc_t)(CUdeviceptr*, size_t);
typedef CUresult (CUDAAPI *cuMemFree_t)(CUdeviceptr);
typedef CUresult (CUDAAPI *cuMemcpyHtoD_t)(CUdeviceptr, const void*, size_t);
typedef CUresult (CUDAAPI *cuMemcpyDtoH_t)(void*, CUdeviceptr, size_t);
typedef CUresult (CUDAAPI *cuLaunchKernel_t)(CUfunction, unsigned, unsigned, unsigned,
                                           unsigned, unsigned, unsigned,
                                           unsigned, CUstream, void**, void**);
typedef CUresult (CUDAAPI *cuModuleGetFunction_t)(CUfunction*, CUmodule, const char*);
typedef CUresult (CUDAAPI *cuMemAdvise_t)(CUdeviceptr, size_t, CUmem_advise, CUdevice);
typedef CUresult (CUDAAPI *cuPointerGetAttribute_t)(void*, CUpointer_attribute, CUdeviceptr);
static HMODULE cudaModule = nullptr;
static cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
static cuMemFree_t pOriginal_cuMemFree = nullptr;
static cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
static cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
static cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;
static cuModuleGetFunction_t pOriginal_cuModuleGetFunction = nullptr;
static cuMemAdvise_t pOriginal_cuMemAdvise = nullptr;
static cuPointerGetAttribute_t pOriginal_cuPointerGetAttribute = nullptr;

// 初始化原始函数
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

// Hooked_cuModuleGetFunction
CUresult CUDAAPI Hooked_cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod, const char* name) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    CUresult res = pOriginal_cuModuleGetFunction(hfunc, hmod, name);
    if (res == CUDA_SUCCESS) {
        g_function_name_map[*hfunc] = name;
        std::cout << "[Hook] Mapped CUfunction " << *hfunc << " to name '" << name << "'" << std::endl;
    }
    return res;
}

// 修改后的Hooked_cuMemAlloc
CUresult CUDAAPI Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    std::lock_guard<std::mutex> lock(g_api_mutex);

    // 获取当前NUMA节点
    int current_numaId = numa_node_of_cpu(sched_getcpu());
    if (current_numaId < 0) current_numaId = 0;
    
    // 使用内存服务分配内存
    auto allocation = g_memory_service.allocateMemory(byte_size, current_numaId);
    if (allocation.error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Allocation failed: " << allocation.error << std::endl;
        return allocation.error;
    }
    
    // 在本地NUMA节点分配主机内存
    void* host_ptr = numa_alloc_local(byte_size);
    if (!host_ptr) {
        std::cerr << "[Hook] NUMA allocation failed" << std::endl;
        return CUDA_ERROR_OUT_OF_MEMORY;
    }
    
    // 创建内存映射
    MemoryHandle handle;
    handle.localPtr = reinterpret_cast<CUdeviceptr>(host_ptr);
    handle.remoteHandle = allocation.handle;
    handle.nodeId = allocation.numaNode;
    handle.isGpuMemory = true;
    handle.numaNode = current_numaId;
    handle.size = byte_size;
    
    // 保存映射关系
    *dev_ptr = handle.localPtr;
    g_memory_mapper.AddMapping(handle.localPtr, handle);
    
    // 记录分配访问
    CoolingService::Instance().RecordAccess(handle.localPtr);
    CoolingService::Instance().UpdateMobility(handle.localPtr, 0);
    
    std::cout << "[Hook] Allocated " << byte_size << " bytes on NUMA " << current_numaId 
              << ", localPtr: 0x" << std::hex << handle.localPtr << std::dec
              << ", remoteHandle: " << handle.remoteHandle
              << ", nodeId: " << handle.nodeId << std::endl;
    
    return CUDA_SUCCESS;
}

// 修改后的Hooked_cuMemFree
CUresult CUDAAPI Hooked_cuMemFree(CUdeviceptr dptr) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(dptr);
    if (!handle) {
        return CUDA_ERROR_INVALID_VALUE;
    }
    
    // 释放本地NUMA内存
    if (handle->localPtr) {
        numa_free(reinterpret_cast<void*>(handle->localPtr), handle->size);
    }
    
    // 使用内存服务释放内存
    auto error = g_memory_service.freeMemory(handle->remoteHandle);
    if (error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Free failed: " << error << std::endl;
        return error;
    }
    
    // 移除内存映射
    g_memory_mapper.RemoveMapping(dptr);
    std::cout << "[Hook] Freed memory for fakePtr: 0x" 
              << std::hex << dptr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// 修改后的Hooked_cuMemcpyHtoD
CUresult CUDAAPI Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(dstDevice);
    if (!handle) {
        return pOriginal_cuMemcpyHtoD(dstDevice, srcHost, ByteCount);
    }

    // NUMA优化
    if (handle->numaNode >= 0) {
        numa_run_on_node(handle->numaNode);
    }
    
    int src_numa = -1;
    get_mempolicy(&src_numa, NULL, 0, (void*)srcHost, MPOL_F_NODE | MPOL_F_ADDR);
    
    if (src_numa >= 0 && handle->numaNode >= 0 && src_numa != handle->numaNode) {
        std::cout << "[Hook] Migrating memory from NUMA " << src_numa 
                  << " to NUMA " << handle->numaNode << std::endl;
        void* migrated_ptr = numa_alloc_local(ByteCount);
        if (migrated_ptr) {
            memcpy(migrated_ptr, srcHost, ByteCount);
            srcHost = migrated_ptr;
        }
    }

    // 记录目标地址访问
    CoolingService::Instance().RecordAccess(dstDevice);
    
    // 使用传输服务执行传输
    auto result = g_transport_service.executeTransfer(
        reinterpret_cast<uint64_t>(srcHost),
        dstDevice,
        ByteCount,
        TransportService::HOST_TO_DEVICE
    );
    
    if (result != CUDA_SUCCESS) {
        std::cerr << "[Hook] HtoD transfer failed: " << result << std::endl;
        return result;
    }
    
    return CUDA_SUCCESS;
}

// 修改后的Hooked_cuMemcpyDtoH
CUresult CUDAAPI Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 使用传输服务执行传输
    auto result = g_transport_service.executeTransfer(
        srcDevice,
        reinterpret_cast<uint64_t>(dstHost),
        ByteCount,
        TransportService::DEVICE_TO_HOST
    );
    
    if (result != CUDA_SUCCESS) {
        std::cerr << "[Hook] Transfer failed: " << result << std::endl;
        return result;
    }
    
    return CUDA_SUCCESS;
}

// 完整的Hooked_cuLaunchKernel实现
CUresult CUDAAPI Hooked_cuLaunchKernel(
    CUfunction f,
    unsigned gridDimX, unsigned gridDimY, unsigned gridDimZ,
    unsigned blockDimX, unsigned blockDimY, unsigned blockDimZ,
    unsigned sharedMemBytes, CUstream hStream,
    void** kernelParams, void** extra) {

    std::lock_guard<std::mutex> lock(g_api_mutex);

    // 查找内核名称
    auto it = g_function_name_map.find(f);
    if (it == g_function_name_map.end()) {
        std::cerr << "[Hook] Unknown kernel function" << std::endl;
        return CUDA_ERROR_INVALID_HANDLE;
    }
    const std::string& kernelName = it->second;
    
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
        // 使用传输服务启动内核
        auto result = g_transport_service.launchKernel(
            kernelName,
            gridDimX, gridDimY, gridDimZ,
            blockDimX, blockDimY, blockDimZ,
            sharedMemBytes,
            launch.getParams()
        );
        
        return result;
    } catch (const kj::Exception& e) {
        std::cerr << "[Hook] Exception in LaunchKernel: " << e.getDescription().cStr() << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
}

// 修改后的InitializeHook
void InitializeHook() {
    InitOriginalFunctions();
    
    // 初始化服务连接
    g_memory_service.connect("127.0.0.1:12345");
    g_transport_service.connect("127.0.0.1:12345");
    
    // 启动状态监控线程
    g_status_thread = std::thread([](){
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            // 状态监控逻辑
        }
    });
    
    // 安装hook
    pOriginal_cuMemAdvise = hookFunction("cuMemAdvise", Hooked_cuMemAdvise);
    pOriginal_cuPointerGetAttribute = hookFunction("cuPointerGetAttribute", Hooked_cuPointerGetAttribute);
    
    std::cout << "[Hook] Initialized" << std::endl;
    CoolingService::Instance().Start();
}

// CleanupHook实现
void CleanupHook() {
    // 清理资源
    if (g_status_thread.joinable()) {
        g_status_thread.join();
    }
    
    if (cudaModule) {
        FreeLibrary(cudaModule);
        cudaModule = nullptr;
    }
    
    std::cout << "[Hook] Cleaned up" << std::endl;
}

// 修改后的Hooked_cuMemAdvise
CUresult CUDAAPI Hooked_cuMemAdvise(CUdeviceptr devPtr, size_t count, CUmem_advise advice, CUdevice device) {
    if (advice == CU_MEM_ADVISE_SET_PREFERRED_LOCATION || advice == CU_MEM_ADVISE_SET_ACCESSED_BY) {
        std::lock_guard<std::mutex> lock(g_api_mutex);
        try {
            std::cout << "[Hook] Forwarding prefetch advice for fakePtr: 0x" 
                      << std::hex << devPtr << std::dec << std::endl;
            g_advise_service.handleMemAdvise(devPtr, count, advice);
        } catch (const std::exception& e) {
            std::cerr << "[Hook] Exception in cuMemAdvise: " << e.what() << std::endl;
        }
    }
    return pOriginal_cuMemAdvise(devPtr, count, advice, device);
}

// 修改后的Hooked_cuPointerGetAttribute
CUresult CUDAAPI Hooked_cuPointerGetAttribute(void* data, CUpointer_attribute attribute, CUdeviceptr ptr) {
    if (attribute == CU_POINTER_ATTRIBUTE_DEVICE_ORDINAL) {
        std::lock_guard<std::mutex> lock(g_api_mutex);
        try {
            auto location = g_memory_service.getMemoryLocation(ptr);
            if (location.error == CUDA_SUCCESS) {
                *(static_cast<int*>(data)) = location.numaNode;
                std::cout << "[Hook] Got remote device location for fakePtr 0x" << std::hex << ptr
                          << ": Node " << location.numaNode << std::dec << std::endl;
                return CUDA_SUCCESS;
            } else {
                return location.error;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Hook] Exception in cuPointerGetAttribute: " << e.what() << std::endl;
            return CUDA_ERROR_LAUNCH_FAILED;
        }
    }
    return pOriginal_cuPointerGetAttribute(data, attribute, ptr);
}

// DllMain实现
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: 
        InitializeHook(); 
        break;
    case DLL_PROCESS_DETACH: 
        CleanupHook(); 
        break;
    }
    return TRUE;
}
