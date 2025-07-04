#include "hook_cuda.h"
#include "launcher_client.h"
#include "zmq_manager.h"
#include "context_manager.h" // 添加内存映射管理
#include "proto/hook-launcher.capnp.h"
#include "proto/cuda.capnp.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <kj/common.h>
#include <kj/debug.h>

using json = nlohmann::json;

// 定义MemoryHandle结构体
struct MemoryHandle {
    CUdeviceptr localPtr;
    uint64_t remoteHandle;
    int nodeId;
};

// 全局资源声明
// 全局资源声明
static std::unique_ptr<LauncherClient> g_launcher_client;
static std::unique_ptr<ZmqManager> g_zmq_manager;
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
static HMODULE cudaModule = nullptr;
static cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
static cuMemFree_t pOriginal_cuMemFree = nullptr;
static cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
static cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
static cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;
static cuModuleGetFunction_t pOriginal_cuModuleGetFunction = nullptr;

// 加载原始函数
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

// Hooked cuModuleGetFunction
CUresult CUDAAPI Hooked_cuModuleGetFunction(CUfunction* hfunc, CUmodule hmod, const char* name) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    CUresult res = pOriginal_cuModuleGetFunction(hfunc, hmod, name);
    if (res == CUDA_SUCCESS) {
        g_function_name_map[*hfunc] = name;
        std::cout << "[Hook] Mapped CUfunction " << *hfunc << " to name '" << name << "'" << std::endl;
    }
    return res;
}

// 状态监控线程实现
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

// 完整的Hooked cuMemAlloc实现
CUresult CUDAAPI Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 使用新协议分配内存
    auto handle = g_launcher_client->requestAllocationV2(byte_size);
    if (handle.error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Allocation failed: " << handle.error << std::endl;
        return CUDA_ERROR_OUT_OF_MEMORY;
    }
    
    // 保存MemoryHandle映射
    *dev_ptr = handle.localPtr;
    g_memory_mapper.AddMapping(handle.localPtr, handle);
    
    std::cout << "[Hook] Allocated " << byte_size << " bytes, localPtr: 0x" 
              << std::hex << handle.localPtr << std::dec
              << ", remoteHandle: " << handle.remoteHandle
              << ", nodeId: " << handle.nodeId << std::endl;
    
    std::cout << "[Hook] Allocated " << byte_size << " bytes, fakePtr: 0x" 
              << std::hex << result.fakePtr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// 完整的Hooked cuMemFree实现
CUresult CUDAAPI Hooked_cuMemFree(CUdeviceptr dptr) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 获取内存映射信息（使用MemoryHandle）
    auto* handle = g_memory_mapper.GetMapping(dptr);
    if (!handle) {
        return CUDA_ERROR_INVALID_VALUE;
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

// 完整的Hooked cuMemcpyHtoD实现
CUresult CUDAAPI Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;
    
    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(dstDevice);
    if (!handle) {
        return pOriginal_cuMemcpyHtoD(dstDevice, srcHost, ByteCount);
    }
    
    // 创建传输参数
    MemcpyParams params;
    params.setDstPtr(reinterpret_cast<uint64_t>(dstDevice));
    params.setSrcPtr(reinterpret_cast<uint64_t>(srcHost));
    params.setSize(ByteCount);
    params.setKind(static_cast<uint32_t>(cudaMemcpyHostToDevice));
    
    // 执行数据传输
    auto result = g_launcher_client->requestMemcpyV2(params);
    if (result != CUDA_SUCCESS) {
        std::cerr << "[Hook] HtoD transfer failed: " << result << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    return CUDA_SUCCESS;
}

// 完整的Hooked cuMemcpyDtoH实现
CUresult CUDAAPI Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    if (!g_launcher_client || !g_launcher_client->isConnected())
        return CUDA_ERROR_NOT_INITIALIZED;

    // 获取内存映射信息
    auto* handle = g_memory_mapper.GetMapping(srcDevice);
    if (!handle) {
        return pOriginal_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
    }
    
    // 创建传输参数
    MemcpyParams params;
    params.setSrcPtr(reinterpret_cast<uint64_t>(srcDevice));
    params.setDstPtr(reinterpret_cast<uint64_t>(dstHost));
    params.setSize(ByteCount);
    params.setKind(static_cast<uint32_t>(cudaMemcpyDeviceToHost));
    
    // 执行数据传输
    auto result = g_launcher_client->requestMemcpyV2(params);
    if (result != CUDA_SUCCESS) {
        std::cerr << "[Hook] DtoH transfer failed: " << result << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    return CUDA_SUCCESS;
}

// 完整的Hooked cuLaunchKernel实现
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
            paramBuilder.setType(launch.getParams()[i].getType());
            paramBuilder.setValue(launch.getParams()[i].getValue());
            paramBuilder.setOffset(0); // 缓冲区偏移（暂未使用）
            paramBuilder.setAlignment(4); // 对齐字节（默认4）
        }

        auto response = req.send().wait(*g_wait_scope);
        auto err = response.getError();
        return static_cast<CUresult>(err);

    } catch (const kj::Exception& e) {
        std::cerr << "[Hook] Exception in LaunchKernel: " << e.getDescription().cStr() << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
}

// 初始化与清理
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
    
    // 启动状态监控线程
    g_status_thread = std::thread(StatusMonitorThread);
    
    std::cout << "[Hook] Initialized" << std::endl;
    std::lock_guard<std::mutex> lock(g_api_mutex);
    InitOriginalFunctions();
    g_wait_scope = std::make_unique<kj::WaitScope>();
    g_launcher_client = std::make_unique<LauncherClient>();
    if (!g_launcher_client->connect("127.0.0.1:12345", 3)) {
        std::cerr << "[Hook] Launcher connect failed" << std::endl;
        g_launcher_client.reset();
    }
    g_zmq_manager = std::make_unique<ZmqManager>();
    if (!g_zmq_manager->Initialize()) {
        std::cerr << "[Hook] ZMQ init failed" << std::endl;
        g_zmq_manager.reset();
        g_launcher_client.reset();
    }
    std::cout << "[Hook] Initialized" << std::endl;
}

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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH: InitializeHook(); break;
    case DLL_PROCESS_DETACH: CleanupHook(); break;
    }
    return TRUE;
}
