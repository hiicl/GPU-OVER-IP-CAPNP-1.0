#pragma once

#include "common.capnp.h"  // 更新包含路径
#include "hook-launcher.capnp.h"  // 更新包含路径
#include <capnp/ez-rpc.h>
#include <memory>
#include <string>  // 添加缺失的头文件
#include <cstdint>  // 添加缺失的头文件

class LauncherClient {
public:
    LauncherClient(const std::string& address);
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
    
private:
    std::string m_address;
    std::unique_ptr<capnp::EzRpcClient> m_rpcClient;
    hook_launcher::HookLauncher::Client m_client;
};
