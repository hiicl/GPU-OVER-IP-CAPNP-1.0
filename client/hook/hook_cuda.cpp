#include "hook_cuda.h"
#include "launcher_client.h"  // 本地IPC客户端
#include "hook-launcher.capnp.h"  // 协议定义
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <iostream>
#include <mutex>
#include <vector>
#include <cstring>
#include <sstream>
#include <nlohmann/json.hpp>
using Direction = ::capnp::schemas::Direction_fc061cce9bf5918c;
using common = ::capnp::schemas::common_0xdefdefdefdefdef0;  // 添加common命名空间

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

// 原始函数指针声明
cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
cuMemFree_t pOriginal_cuMemFree = nullptr;
cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;

CUresult __stdcall Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    // 请求Launcher分配内存
    auto result = g_launcher_client->requestAllocation(byte_size);
    if (result.error != common::ErrorCode::OK) {
        std::cerr << "[Hook] Failed to allocate memory: " 
                  << static_cast<int>(result.error) << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    // 保存伪造指针映射
    *dev_ptr = result.fakePtr;
    std::cout << "[Hook] Allocated " << byte_size << " bytes, fakePtr: 0x" 
              << std::hex << result.fakePtr << std::dec << std::endl;
    
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

// 内存传输实现
CUresult __stdcall Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    // 请求传输计划
    auto plan = g_launcher_client->planMemcpyHtoD(dstDevice, ByteCount);
    if (plan.error != common::ErrorCode::OK) {
        std::cerr << "[Hook] Failed to get transfer plan: " 
                  << static_cast<int>(plan.error) << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    // 直接使用RDMA传输数据（零拷贝）
    // 这里使用RoCE RDMA引擎进行数据传输
    RDMA_Transfer(
        srcHost, 
        plan.targetServerIp.cStr(), 
        plan.targetServerRdmaPort,
        plan.remotePtr,
        ByteCount
    );
    
    return CUDA_SUCCESS;
}

CUresult __stdcall Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    auto info = g_memory_mapper.GetMapping(srcDevice);
    if (!info) {
        return pOriginal_cuMemcpyDtoH(dstHost, srcDevice, ByteCount);
    }
    
    RemoteNode* node = g_dispatcher.GetNodeById(info->node_id);
    if (!node) {
        return CUDA_ERROR_UNKNOWN;
    }
    
    g_capnp_client->cudaMemcpy(
        info->remote_handle,
        reinterpret_cast<uint64_t>(dstHost),
        ByteCount,
        Direction::DEVICE_TO_HOST
    );
    
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
    RemoteNode* node = g_dispatcher.GetDefaultNode();
    if (!node) return CUDA_ERROR_UNKNOWN;

    // 序列化内核参数为JSON
    json kernel_config;
    kernel_config["gridDim"] = {gridDimX, gridDimY, gridDimZ};
    kernel_config["blockDim"] = {blockDimX, blockDimY, blockDimZ};
    kernel_config["sharedMemBytes"] = sharedMemBytes;
    kernel_config["stream"] = reinterpret_cast<uint64_t>(hStream);
    kernel_config["params"] = json::array();

    if (kernelParams) {
        for (int i = 0; kernelParams[i] != nullptr; i++) {
            kernel_config["params"].push_back(reinterpret_cast<uint64_t>(kernelParams[i]));
        }
    }

    std::string config_str = kernel_config.dump();
    
    auto response = g_capnp_client->cudaKernelLaunch(
        "default-gpu",
        gridDimX, gridDimY, gridDimZ,
        blockDimX, blockDimY, blockDimZ,
        sharedMemBytes,
        config_str
    );
    
    if (response.getExitCode() != 0) {
        std::cerr << "[Hook] Kernel launch failed: " 
                  << response.getOutput().cStr() << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
    
    // 检查协议定义的错误码
    if (response.getErrorCode() != common::ErrorCode::OK) {
        std::cerr << "[Hook] Kernel launch error: " 
                  << static_cast<int>(response.getErrorCode()) << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
    return CUDA_SUCCESS;
}

// 状态监控线程
void StatusMonitorThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // 收集所有节点状态
        for (auto& node : g_dispatcher.GetNodes()) {
            auto status = g_capnp_client->getGpuStatus(node.id);
            node.available_memory = status.getAvailableMemory();
            node.gpu_utilization = status.getUtilization();
        }
    }
}

// 初始化函数 - 在DLL加载时调用
void InitializeHook() {
    // 创建Launcher客户端（连接到本地IPC）
    g_launcher_client = std::make_unique<LauncherClient>("127.0.0.1:12345");
    
    // 初始化连接
    if (!g_launcher_client->connect()) {
        std::cerr << "[Hook] Failed to connect to launcher" << std::endl;
    }
}

// 清理函数 - 在DLL卸载时调用
void CleanupHook() {
    g_capnp_client.reset();
}
