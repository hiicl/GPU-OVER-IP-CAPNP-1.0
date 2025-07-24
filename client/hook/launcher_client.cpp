// 将文件保存为 UTF-8 编码以修复编码问题
#include "pch.h" // 预编译头必须放在最前面
#include "launcher_client.h" // 当前模块专用头文件

LauncherClient::LauncherClient(const std::string& address) 
    : m_address(address) {}

bool LauncherClient::Connect(const std::string& address) {
    m_address = address; // 更新地址
    return connect(); // 调用现有的连接逻辑
}

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

// ===== 实现新增高级功能方法 =====
hook_launcher::NodeInfo::Reader LauncherClient::getMemoryLocation(uint64_t fakePtr) {
    auto request = m_client.getMemoryLocationRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getLocation();
}

common::ErrorCode LauncherClient::advisePrefetch(uint64_t fakePtr) {
    auto request = m_client.advisePrefetchRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getAck().getError();
}
