// CUDA API Hook实现模块 - 重构版本
// 将文件保存为 UTF-8 编码以修复编码问题
#include "pch.h" // 预编译头必须放在最前面
#include "hook_cuda.h"
#include "hook-launcher.capnp.h"
#include "cuda.capnp.h"
#include "../launcher/launcher_client.h" // 新增LauncherClient头文件
#include "../data_transfer/include/data_transfer.h" // 添加数据传输模块头文件
#include <iostream>
#include <mutex>
#include <map>
#include <string>
#include <kj/common.h>
#include <kj/debug.h>

using namespace hook_launcher;

// 全局资源声明
static std::mutex g_api_mutex; // 添加API互斥锁
static std::map<CUfunction, std::string> g_function_name_map;
static std::thread g_status_thread;
static LauncherClient g_launcher_client; // 使用LauncherClient替代本地服务

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
static HMODULE cudaModule = nullptr;
static cuMemAlloc_t pOriginal_cuMemAlloc = nullptr;
static cuMemFree_t pOriginal_cuMemFree = nullptr;
static cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD = nullptr;
static cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH = nullptr;
static cuLaunchKernel_t pOriginal_cuLaunchKernel = nullptr;
static cuModuleGetFunction_t pOriginal_cuModuleGetFunction = nullptr;

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

// 简化的Hooked_cuMemAlloc
CUresult CUDAAPI Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 通过RPC调用远程分配内存
    auto result = g_launcher_client.AllocateMemory(byte_size);
    if (result.error != CUDA_SUCCESS) {
        std::cerr << "[Hook] Remote allocation failed: " << result.error << std::endl;
        return result.error;
    }
    
    *dev_ptr = result.device_ptr;
    std::cout << "[Hook] Allocated " << byte_size << " bytes remotely, ptr: 0x" 
              << std::hex << *dev_ptr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// 简化的Hooked_cuMemFree
CUresult CUDAAPI Hooked_cuMemFree(CUdeviceptr dptr) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 通过RPC调用远程释放内存
    auto result = g_launcher_client.FreeMemory(dptr);
    if (result != CUDA_SUCCESS) {
        std::cerr << "[Hook] Remote free failed: " << result << std::endl;
        return result;
    }
    
    std::cout << "[Hook] Freed memory for ptr: 0x" 
              << std::hex << dptr << std::dec << std::endl;
    
    return CUDA_SUCCESS;
}

// 简化的Hooked_cuMemcpyHtoD
CUresult CUDAAPI Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 直接调用数据传输模块
    bool success = SendData("127.0.0.1", 5555, srcHost, ByteCount);
    
    if (!success) {
        std::cerr << "[Hook] HtoD transfer failed" << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    return CUDA_SUCCESS;
}

// 简化的Hooked_cuMemcpyDtoH
CUresult CUDAAPI Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount) {
    std::lock_guard<std::mutex> lock(g_api_mutex);
    
    // 直接调用数据传输模块
    bool success = ReceiveData("127.0.0.1", 5555, dstHost, ByteCount);
    
    if (!success) {
        std::cerr << "[Hook] DtoH transfer failed" << std::endl;
        return CUDA_ERROR_UNKNOWN;
    }
    
    return CUDA_SUCCESS;
}

// 简化的Hooked_cuLaunchKernel实现
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
    
    try {
        // 通过RPC调用远程启动内核
        auto result = g_launcher_client.LaunchKernel(
            kernelName,
            gridDimX, gridDimY, gridDimZ,
            blockDimX, blockDimY, blockDimZ,
            sharedMemBytes,
            kernelParams
        );
        
        return result;
    } catch (const kj::Exception& e) {
        std::cerr << "[Hook] Exception in LaunchKernel: " << e.getDescription().cStr() << std::endl;
        return CUDA_ERROR_LAUNCH_FAILED;
    }
}

// 简化的InitializeHook
void InitializeHook() {
    InitOriginalFunctions();
    
    // 初始化LauncherClient
    g_launcher_client.Connect("127.0.0.1:12345");
    
    std::cout << "[Hook] Initialized with LauncherClient" << std::endl;
}

// CleanupHook实现
void CleanupHook() {
    // 断开LauncherClient连接
    g_launcher_client.Disconnect();
    
    if (cudaModule) {
        FreeLibrary(cudaModule);
        cudaModule = nullptr;
    }
    
    std::cout << "[Hook] Cleaned up" << std::endl;
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
