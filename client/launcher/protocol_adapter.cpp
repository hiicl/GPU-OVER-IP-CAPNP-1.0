#include "protocol_adapter.h"
#include "cuda.capnp.h"

MemoryHandle ProtocolAdapter::convertToMemoryHandle(const RemoteAllocInfo& info) {
    MemoryHandle handle;
    handle.setLocalPtr(info.fakePtr);
    handle.setRemoteHandle(info.remote_handle);
    handle.setNodeId(info.node_id);
    return handle;
}

RemoteAllocInfo ProtocolAdapter::convertToRemoteAllocInfo(const MemoryHandle& handle) {
    return RemoteAllocInfo{
        .node_id = handle.getNodeId(),
        .size = handle.getSize(), // 需要从服务端获取大小
        .remote_handle = handle.getRemoteHandle(),
        .fakePtr = handle.getLocalPtr()
    };
}

KernelLaunch ProtocolAdapter::convertToKernelLaunch(
    const std::string& kernelName,
    unsigned gridDimX, unsigned gridDimY, unsigned gridDimZ,
    unsigned blockDimX, unsigned blockDimY, unsigned blockDimZ,
    unsigned sharedMemBytes, 
    void** kernelParams) {
    
    KernelLaunch launch;
    launch.setKernelName(kernelName);
    launch.setGridDimX(gridDimX);
    launch.setGridDimY(gridDimY);
    launch.setGridDimZ(gridDimZ);
    launch.setBlockDimX(blockDimX);
    launch.setBlockDimY(blockDimY);
    launch.setBlockDimZ(blockDimZ);
    launch.setSharedMemBytes(sharedMemBytes);
    
    // 转换参数
    capnp::List<KernelParam>::Builder paramsBuilder = launch.initParams(MAX_PARAMS);
    for (int i = 0; i < MAX_PARAMS && kernelParams[i] != nullptr; ++i) {
        KernelParam::Builder param = paramsBuilder[i];
        param.setValue(reinterpret_cast<uint64_t>(kernelParams[i]));
    }
    
    return launch;
}
