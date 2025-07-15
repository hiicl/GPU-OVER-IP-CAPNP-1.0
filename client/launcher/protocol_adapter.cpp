#include "pch.h"
#include "protocol_adapter.h"
#include "cuda.capnp.h"
#ifdef ENABLE_RDMA
#include <infiniband/verbs.h> // RDMA核心库
#include <rdma/rdma_cma.h>    // RDMA通信管理器
#endif

MemoryHandle ProtocolAdapter::convertToMemoryHandle(const RemoteAllocInfo& info) {
    MemoryHandle handle;
    handle.setLocalPtr(info.fakePtr);
    handle.setRemoteHandle(info.remote_handle);
    handle.setNodeId(info.node_id);
    return handle;
}
#ifdef ENABLE_RDMA
// RDMA内存复制函数
void rdmaMemcpy(void* dst, const void* src, size_t count, enum rdma_memcpy_type type) {
    // 创建RDMA上下文
    struct ibv_context* context = ibv_open_device();
    if (!context) {
        fprintf(stderr, "无法打开RDMA设备\n");
        return;
    }
    
    // 创建保护域
    struct ibv_pd* pd = ibv_alloc_pd(context);
    if (!pd) {
        fprintf(stderr, "无法分配保护域\n");
        ibv_close_device(context);
        return;
    }
    
    // 注册内存区域
    struct ibv_mr* mr_src = ibv_reg_mr(pd, (void*)src, count, 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!mr_src) {
        fprintf(stderr, "无法注册源内存区域\n");
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        return;
    }
    
    struct ibv_mr* mr_dst = ibv_reg_mr(pd, dst, count, 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
    if (!mr_dst) {
        fprintf(stderr, "无法注册目标内存区域\n");
        ibv_dereg_mr(mr_src);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        return;
    }
    
    // 创建完成队列
    struct ibv_cq* cq = ibv_create_cq(context, 10, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "无法创建完成队列\n");
        ibv_dereg_mr(mr_dst);
        ibv_dereg_mr(mr_src);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        return;
    }
    
    // 创建队列对
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .cap = {
            .max_send_wr = 10,
            .max_recv_wr = 10,
            .max_send_sge = 1,
            .max_recv_sge = 1
        },
        .qp_type = IBV_QPT_RC
    };
    
    struct ibv_qp* qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        fprintf(stderr, "无法创建队列对\n");
        ibv_destroy_cq(cq);
        ibv_dereg_mr(mr_dst);
        ibv_dereg_mr(mr_src);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        return;
    }
    
    // RDMA传输逻辑
    // (实际实现需要建立连接、交换元数据等)
    
    // 清理资源
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr_dst);
    ibv_dereg_mr(mr_src);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);
}
#endif

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
    
    // 转换参数 - 保留指针值但添加内存位置信息
    int paramCount = 0;
    while (kernelParams[paramCount] != nullptr && paramCount < MAX_PARAMS) {
        paramCount++;
    }
    
    capnp::List<KernelParam>::Builder paramsBuilder = launch.initParams(paramCount);
    for (int i = 0; i < paramCount; ++i) {
        KernelParam::Builder param = paramsBuilder[i];
        
        // 保留原始指针值（用于零拷贝优化）
        uint64_t ptrValue = reinterpret_cast<uint64_t>(kernelParams[i]);
        param.setValue(ptrValue);
        
        // 添加内存位置元数据
        auto location = g_launcher_client->getMemoryLocation(ptrValue);
        param.setNodeId(location.nodeId);
        param.setMemoryType(location.memoryType);
    }
    
    return launch;
}
