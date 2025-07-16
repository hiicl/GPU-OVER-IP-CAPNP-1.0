#include "pch.h"
#include "protocol_adapter.h"
#include "cuda.capnp.h"
#ifdef ENABLE_RDMA
#include <infiniband/verbs.h> // RDMA核心库
#include <rdma/rdma_cma.h>    // RDMA通信管理器
#endif

MemoryHandle ProtocolAdapter::convertToMemoryHandle(const RemoteAllocInfo& info) {
    MemoryHandle handle;
    auto id = handle.initId();
    id.setHandle(info.remote_handle); // 使用统一ID类型
    handle.setSize(info.size);
    handle.setNodeId(info.node_id);
    handle.setLocalPtr(info.fakePtr);
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
        .size = handle.getSize(),
        .remote_handle = handle.getId().getHandle(), // 从统一ID获取
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
    
    // 计算参数数量
    int paramCount = 0;
    while (kernelParams[paramCount] != nullptr && paramCount < MAX_PARAMS) {
        paramCount++;
    }
    
    capnp::List<KernelParam>::Builder paramsBuilder = launch.initParams(paramCount);
    for (int i = 0; i < paramCount; ++i) {
        KernelParam::Builder param = paramsBuilder[i];
        void* paramPtr = kernelParams[i];
        
        // 判断参数类型：指针还是标量
        uint64_t paramValue = reinterpret_cast<uint64_t>(paramPtr);
        auto location = g_launcher_client->getMemoryLocation(paramValue);
        
        if (location.nodeId != 0xFFFFFFFF) { // 有效内存位置
            param.setType(ParamType::pointer);
            
            // 存储指针值和内存位置信息
            auto valueBuilder = param.initValue(sizeof(uint64_t));
            memcpy(valueBuilder.begin(), &paramValue, sizeof(uint64_t));
            
            param.setNodeId(location.nodeId);
            param.setMemoryType(location.memoryType);
        } else {
            param.setType(ParamType::scalar);
            
            // 假设标量参数大小为8字节（处理int, float, double等）
            auto valueBuilder = param.initValue(sizeof(uint64_t));
            memcpy(valueBuilder.begin(), paramPtr, sizeof(uint64_t));
        }
        
        // 设置默认对齐和偏移
        param.setAlignment(8); // 8字节对齐
        param.setOffset(0);
    }
    
    return launch;
}
