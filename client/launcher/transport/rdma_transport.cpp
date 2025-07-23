#include "rdma_transport.h"
#include <iostream>

// 传输类型定义
enum TransferType {
    STANDARD = 0,
    GDR = 1
};

RdmaTransport::RdmaTransport() {}

RdmaTransport::~RdmaTransport() {
    Cleanup();
}

bool RdmaTransport::Initialize() {
    // 获取RDMA设备列表
    ibv_device** dev_list = ibv_get_device_list(nullptr);
    if (!dev_list) {
        std::cerr << "Failed to get IB devices list" << std::endl;
        return false;
    }
    
    // 选择第一个可用设备
    for (ibv_device** p = dev_list; *p; ++p) {
        m_context = ibv_open_device(*p);
        if (m_context) break;
    }
    
    ibv_free_device_list(dev_list);
    
    if (!m_context) {
        std::cerr << "No RDMA device available" << std::endl;
        return false;
    }
    
    return CreateProtectionDomain();
}

bool RdmaTransport::CreateProtectionDomain() {
    m_protectionDomain = ibv_alloc_pd(m_context);
    if (!m_protectionDomain) {
        std::cerr << "Failed to allocate protection domain" << std::endl;
        return false;
    }
    return true;
}

bool RdmaTransport::RegisterMemory(void* ptr, size_t size) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 检查是否已注册
    if (m_memoryRegions.find(ptr) != m_memoryRegions.end()) {
        return true;
    }
    
    // 注册内存区域
    ibv_mr* mr = ibv_reg_mr(m_protectionDomain, ptr, size,
                            IBV_ACCESS_LOCAL_WRITE |
                            IBV_ACCESS_REMOTE_WRITE |
                            IBV_ACCESS_REMOTE_READ);
    if (!mr) {
        std::cerr << "Failed to register memory region" << std::endl;
        return false;
    }
    
    m_memoryRegions[ptr] = mr;
    return true;
}

bool RdmaTransport::Transfer(void* localBuffer, size_t bufferSize, 
                          uint64_t remoteAddr, uint32_t remoteKey, 
                          TransferType type) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 查找本地内存区域
    auto it = m_memoryRegions.find(localBuffer);
    if (it == m_memoryRegions.end()) {
        if (!RegisterMemory(localBuffer, bufferSize)) {
            return false;
        }
        it = m_memoryRegions.find(localBuffer);
    }
    
    // 创建队列对
    ibv_qp_init_attr qp_init_attr = {};
    qp_init_attr.send_cq = ibv_create_cq(m_context, 1, nullptr, nullptr, 0);
    qp_init_attr.recv_cq = ibv_create_cq(m_context, 1, nullptr, nullptr, 0);
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr = 1;
    qp_init_attr.cap.max_recv_wr = 1;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    
    ibv_qp* qp = ibv_create_qp(m_protectionDomain, &qp_init_attr);
    if (!qp) {
        std::cerr << "Failed to create queue pair" << std::endl;
        return false;
    }
    
    // 根据传输类型设置操作
    ibv_wr_opcode opcode;
    if (type == TransferType::GDR) {
        // 启用GPU直接RDMA
        if (!EnableGdr(qp)) {
            ibv_destroy_qp(qp);
            return false;
        }
        opcode = IBV_WR_RDMA_WRITE;
    } else {
        opcode = IBV_WR_RDMA_WRITE;
    }
    
    // 执行RDMA操作
    ibv_sge sge;
    sge.addr = reinterpret_cast<uint64_t>(localBuffer);
    sge.length = bufferSize;
    sge.lkey = it->second->lkey;
    
    ibv_send_wr wr = {};
    wr.wr_id = 0;
    wr.opcode = opcode;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remoteAddr;
    wr.wr.rdma.rkey = remoteKey;
    
    ibv_send_wr* bad_wr = nullptr;
    if (ibv_post_send(qp, &wr, &bad_wr)) {
        std::cerr << "Failed to post RDMA operation" << std::endl;
        ibv_destroy_qp(qp);
        return false;
    }
    
    // 等待完成
    ibv_wc wc;
    int ret;
    do {
        ret = ibv_poll_cq(qp_init_attr.send_cq, 1, &wc);
    } while (ret == 0);
    
    ibv_destroy_qp(qp);
    return wc.status == IBV_WC_SUCCESS;
}

bool RdmaTransport::EnableGdr(ibv_qp* qp) {
    #ifdef ENABLE_GDR
    ibv_exp_gid_attr gid_attr = {
        .type = IBV_EXP_GID_ATTR_TYPE_ROCE_V2,
        .is_grh = 1
    };
    if (ibv_exp_modify_qp(qp, &gid_attr, IBV_EXP_QP_GID_ATTR)) {
        std::cerr << "Failed to enable GDR support" << std::endl;
        return false;
    }
    return true;
    #else
    std::cerr << "GDR support not compiled in" << std::endl;
    return false;
    #endif
}

void RdmaTransport::Cleanup() {
    for (auto& pair : m_memoryRegions) {
        ibv_dereg_mr(pair.second);
    }
    m_memoryRegions.clear();
    
    if (m_protectionDomain) {
        ibv_dealloc_pd(m_protectionDomain);
        m_protectionDomain = nullptr;
    }
    
    if (m_context) {
        ibv_close_device(m_context);
        m_context = nullptr;
    }
}
