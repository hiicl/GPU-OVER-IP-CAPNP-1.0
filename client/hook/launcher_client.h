#pragma once

#include "hook-launcher.capnp.h"
#include "common.capnp.h"
#include <capnp/ez-rpc.h>
#include <memory>
#include <string>  
#include <cstdint>  
#include "pch.h"

class LauncherClient {
public:
    LauncherClient(const std::string& address);
    bool Connect(const std::string& address); // 修正为单参数Connect方法
    bool connect();
    
    // HookLauncher接口实现
    hook_launcher::AllocationResult::Reader requestAllocation(uint64_t size);
    common::ErrorCode requestFree(uint64_t fakePtr);
    hook_launcher::MemcpyPlan::Reader planMemcpyHtoD(uint64_t dstFakePtr, uint64_t size);
    hook_launcher::MemcpyPlan::Reader planMemcpyDtoH(uint64_t srcFakePtr, uint64_t size);
    common::ErrorCode launchKernel(const std::string& func, 
                                   uint32_t gridDim, 
                                   uint32_t blockDim,
                                   uint32_t sharedMem,
                                   const void* params);
    
    // ===== 新增高级功能方法 =====
    hook_launcher::NodeInfo::Reader getMemoryLocation(uint64_t fakePtr);
    common::ErrorCode advisePrefetch(uint64_t fakePtr);
    
private:
    std::string m_address;
    std::unique_ptr<capnp::EzRpcClient> m_rpcClient;
    hook_launcher::HookLauncher::Client m_client;
};
