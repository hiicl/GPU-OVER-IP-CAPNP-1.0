#include "launcher_client.h"
#include <kj/debug.h>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <stdexcept>

LauncherClient::LauncherClient(const std::string& address) 
    : m_address(address) {}

bool LauncherClient::connect() {
    try {
        m_rpcClient = std::make_unique<capnp::EzRpcClient>(m_address);
        auto& waitScope = m_rpcClient->getWaitScope();
        m_client = m_rpcClient->getMain<hook_launcher::HookLauncher>();
        return true;
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Failed to connect to launcher", e.what());
        return false;
    }
}

hook_launcher::AllocationResult::Reader LauncherClient::requestAllocation(uint64_t size) {
    auto request = m_client.requestAllocationRequest();
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getResult();
}

common::ErrorCode LauncherClient::requestFree(uint64_t fakePtr) {
    auto request = m_client.requestFreeRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getResult();
}

hook_launcher::MemcpyPlan::Reader LauncherClient::planMemcpyHtoD(uint64_t dstFakePtr, uint64_t size) {
    auto request = m_client.planMemcpyHtoDRequest();
    request.setDstFakePtr(dstFakePtr);
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getPlan();
}

hook_launcher::MemcpyPlan::Reader LauncherClient::planMemcpyDtoH(uint64_t srcFakePtr, uint64_t size) {
    auto request = m_client.planMemcpyDtoHRequest();
    request.setSrcFakePtr(srcFakePtr);
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getPlan();
}

common::ErrorCode LauncherClient::launchKernel(const std::string& func, 
                                              uint32_t gridDim, 
                                              uint32_t blockDim,
                                              uint32_t sharedMem,
                                              const void* params) {
    auto request = m_client.launchKernelRequest();
    request.setFunc(func);
    request.setGridDim(gridDim);
    request.setBlockDim(blockDim);
    request.setSharedMem(sharedMem);
    
    // 正确设置参数数据
    auto paramsBuilder = request.initParams(512); // 分配足够空间
    memcpy(paramsBuilder.begin(), params, 512); // 实际使用时需要知道参数大小
    
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getResult();
}
