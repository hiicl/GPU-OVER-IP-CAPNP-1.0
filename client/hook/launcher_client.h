#pragma once
#include "pch.h" // 预编译头必须放在最前面

// 项目特定头文件
#include "hook-launcher.capnp.h"
#include "common.capnp.h"

// 第三方库
#include <capnp/ez-rpc.h>

// 标准库
#include <memory>
#include <string>
#include <cstdint>

class LauncherClient {
public:
    explicit LauncherClient(const std::string& address);
    bool Connect(const std::string& address); // 修正为单参数Connect方法
    bool connect();
    
    // HookLauncher接口实现
    AllocationPlan::Reader requestAllocationPlan(uint64_t size);
    ErrorCode requestFreePlan(uint64_t fakePtr);
    MemcpyPlan::Reader planMemcpyHtoD(uint64_t dstFakePtr, uint64_t size);
    MemcpyPlan::Reader planMemcpyDtoH(uint64_t srcFakePtr, uint64_t size);
    ErrorCode launchKernel(const std::string& func, 
                          uint32_t gridDimX, uint32_t gridDimY, uint32_t gridDimZ,
                          uint32_t blockDimX, uint32_t blockDimY, uint32_t blockDimZ,
                          uint32_t sharedMemBytes,
                          const void* params);
    
    // ===== 新增高级功能方法 =====
    NodeInfo::Reader getMemoryLocation(uint64_t fakePtr);
    ErrorCode advisePrefetch(uint64_t fakePtr);
    
private:
    std::string m_address;
    std::unique_ptr<capnp::EzRpcClient> m_rpcClient;
    HookLauncher::Client m_client{nullptr}; // 初始化客户端为nullptr
};
