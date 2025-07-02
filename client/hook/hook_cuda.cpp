#include "hook_cuda.h"
#include "launcher_client.h"  // 本地IPC客户端
#include "hook-launcher.capnp.h"  // 协议定义
#include "zmq_manager.h"  // ZMQ管理器
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <cstring>
#include <sstream>
#include <nlohmann/json.hpp>

// 定义函数指针类型
typedef CUresult (CUDAAPI *cuMemAlloc_t)(CUdeviceptr* dptr, size_t bytesize);
typedef CUresult (CUDAAPI *cuMemFree_t)(CUdeviceptr dptr);
typedef CUresult (CUDAAPI *cuMemcpyHtoD_t)(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount);
typedef CUresult (CUDAAPI *cuMemcpyDtoH_t)(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount);
typedef CUresult (CUDAAPI *cuLaunchKernel_t)(
    CUfunction f,
    unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream hStream,
    void** kernelParams, void** extra);

using json = nlohmann::json;

// 全局内存映射器
MemoryMapper g_memory_mapper;

// Launcher客户端实例
std::unique_ptr<LauncherClient> g_launcher_client;

// ZMQ管理器实例
ZmqManager g_zmq_manager;

// 原始函数指针声明
cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
cuMemFree_t pOriginal_cuMemFree = nullptr;
cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;

CUresult __stdcall Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    // 请求Launcher分配内存
    auto result = g_launcher_client->requestAllocation(byte_size);
    if (result.getError() != Cuda::CudaError::SUCCESS) {
        std::cerr << "[Hook] Failed to allocate memory: " 
                  << static_cast<int>(result.getError()) << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    // 保存伪造指针映射
    *dev_ptr = result.getFakePtr();
    std::cout << "[Hook] Allocated " << byte_size << " bytes, fakePtr: 0x" 
              << std::hex << result.getFakePtr() << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

CUresult __stdcall Hooked_cuMemFree(CUdeviceptr dptr) {
    // 请求Launcher释放内存
    auto errorCode = g_launcher_client->requestFree(dptr);
    if (errorCode != common::ErrorCode::OK) {
        std::cerr << "[Hook] Failed to free memory: " 
                  << static_cast<int>(errorCode) << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    std::cout << "[Hook] Freed memory for fakePtr: 0x" 
              << std::hex << dptr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// 内存传输实现 - 使用ZMQ
CUresult __stdcall Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    // 请求传输计划
    auto plan = g_launcher_client->planMemcpyHtoD(dstDevice, ByteCount);
    if (plan.getError() != Cuda::CudaError::SUCCESS) {
        std::cerr << "[Hook] Failed to get transfer plan: " 
                  << static_cast<int>(plan.getError()) << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    // 使用ZMQ传输数据（零拷贝）
    bool transferSuccess = g_zmq_manager.Transfer(
        plan.getTargetServerIp().cStr(), 
        static_cast<USHORT>(plan.getTargetServerZmqPort()),
        const_cast<void*>(srcHost),
        ByteCount
    );
    
    if (!transferSuccess) {
        std::cerr << "[Hook] ZMQ transfer failed" << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    std::cout << "[Hook] ZMQ transfer completed successfully: "
              << ByteCount << " bytes to " 
              << plan.getTargetServerIp().cStr() << ":" 
              << plan.getTargetServerZmqPort() << std::endl;
    
    return CUDA_SUCCESS;
}

CUresult __stdcall Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    // 设备到主机内存复制将在后续任务实现
    // 暂时调用原始函数
    return pOriginal_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
    
    return CUDA_SUCCESS;
}

// 内核启动增强实现
CUresult __stdcall Hooked_cuLaunchKernel(
    CUfunction f,
    unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream hStream,
    void** kernelParams, void** extra) 
{
    // 内核启动将在后续任务实现
    // 暂时调用原始函数
    return pOriginal_cuLaunchKernel(
        f,
        gridDimX, gridDimY, gridDimZ,
        blockDimX, blockDimY, blockDimZ,
        sharedMemBytes, hStream,
        kernelParams, extra);
    return CUDA_SUCCESS;
}

// 状态监控线程 - 将在后续任务实现

// 初始化函数 - 在DLL加载时调用
void InitializeHook() {
    // 创建Launcher客户端（连接到本地IPC）
    g_launcher_client = std::make_unique<LauncherClient>("127.0.0.1:12345");
    
    // 初始化连接
    if (!g_launcher_client->connect()) {
        std::cerr << "[Hook] Failed to connect to launcher" << std::endl;
    }
    
    // 初始化ZMQ管理器
    if (!g_zmq_manager.Initialize()) {
        std::cerr << "[Hook] Failed to initialize ZMQ manager" << std::endl;
    }
}

// 清理函数 - 在DLL卸载时调用
void CleanupHook() {
    // 清理ZMQ资源（析构函数自动处理）
    
    // 清理Launcher客户端
    g_launcher_client.reset();
}
