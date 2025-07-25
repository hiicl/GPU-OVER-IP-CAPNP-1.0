// 将文件保存为 UTF-8 编码以修复编码问题
#include "pch.h" // 预编译头必须放在最前面
#include "launcher_client.h" // 当前模块专用头文件

LauncherClient::LauncherClient(const std::string& address) 
    : m_address(address), m_client(nullptr) {}

bool LauncherClient::connect() {
    try {
        m_rpcClient = std::make_unique<capnp::EzRpcClient>(m_address);
        auto& waitScope = m_rpcClient->getWaitScope();
        m_client = m_rpcClient->getMain<HookLauncher>();
        return true;
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Failed to connect to launcher", e.what());
        return false;
    }
}

AllocationPlan::Reader LauncherClient::requestAllocationPlan(uint64_t size) {
    auto request = m_client.requestAllocationPlanRequest();
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getPlan();
}

ErrorCode LauncherClient::requestFreePlan(uint64_t fakePtr) {
    auto request = m_client.requestFreePlanRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return static_cast<ErrorCode>(response.getAck().getCode());
}

MemcpyPlan::Reader LauncherClient::planMemcpyHtoD(uint64_t dstFakePtr, uint64_t size) {
    auto request = m_client.planMemcpyHtoDRequest();
    auto handle = request.initDstHandle();
    handle.getId().setHandle(dstFakePtr);
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getPlan();
}

MemcpyPlan::Reader LauncherClient::planMemcpyDtoH(uint64_t srcFakePtr, uint64_t size) {
    auto request = m_client.planMemcpyDtoHRequest();
    auto handle = request.initSrcHandle();
    handle.getId().setHandle(srcFakePtr);
    request.setSize(size);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getPlan();
}

ErrorCode LauncherClient::launchKernel(const std::string& func, 
                                      uint32_t gridDimX, 
                                      uint32_t gridDimY,
                                      uint32_t gridDimZ,
                                      uint32_t blockDimX,
                                      uint32_t blockDimY,
                                      uint32_t blockDimZ,
                                      uint32_t sharedMemBytes,
                                      const void* params) {
    auto request = m_client.launchKernelRequest();
    request.setFunc(func);
    request.setGridDimX(gridDimX);
    request.setGridDimY(gridDimY);
    request.setGridDimZ(gridDimZ);
    request.setBlockDimX(blockDimX);
    request.setBlockDimY(blockDimY);
    request.setBlockDimZ(blockDimZ);
    request.setSharedMemBytes(sharedMemBytes);
    
    // 正确设置参数数据
    auto paramsBuilder = request.initParams(512); // 分配足够空间
    memcpy(paramsBuilder.begin(), params, 512); // 实际使用时需要知道参数大小
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return static_cast<ErrorCode>(response.getAck().getCode());
}

// ===== 实现新增高级功能方法 =====
NodeInfo::Reader LauncherClient::getMemoryLocation(uint64_t fakePtr) {
    auto request = m_client.getMemoryLocationRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return response.getLocation();
}

ErrorCode LauncherClient::advisePrefetch(uint64_t fakePtr) {
    auto request = m_client.advisePrefetchRequest();
    request.setFakePtr(fakePtr);
    auto response = request.send().wait(m_rpcClient->getWaitScope());
    return static_cast<ErrorCode>(response.getAck().getCode());
}
