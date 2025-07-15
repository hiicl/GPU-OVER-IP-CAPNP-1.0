#include "rdma_manager.h"
#include <numa.h>
#include <stdexcept>
#include <iostream>

RdmaManager::RdmaManager() {
    // 初始化NUMA库
    if (numa_available() == -1) {
        throw std::runtime_error("NUMA library initialization failed");
    }
}

RdmaManager::~RdmaManager() {
    Cleanup();
}

bool RdmaManager::Initialize(int numa_node) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_numaNode = numa_node;
    
    // 绑定到指定NUMA节点
    numa_run_on_node(m_numaNode);
    
    // 获取RDMA设备列表
    ibv_device** dev_list = ibv_get_device_list(nullptr);
    if (!dev_list) {
        std::cerr << "Failed to get IB devices list" << std::endl;
        return false;
    }
    
    // 选择当前NUMA节点的设备
    for (ibv_device** p = dev_list; *p; ++p) {
        ibv_context* ctx = ibv_open_device(*p);
        if (!ctx) continue;
        
        // 检查设备NUMA亲和性
        int dev_node;
        if (ibv_get_device_numa_node(ctx->device, &dev_node) {
            ibv_close_device(ctx);
            continue;
        }
        
        if (dev_node == m_numaNode) {
            m_context = ctx;
            break;
        }
        ibv_close_device(ctx);
    }
    
    ibv_free_device_list(dev_list);
    
    if (!m_context) {
        std::cerr << "No RDMA device found for NUMA node " << m_numaNode << std::endl;
        return false;
    }
    
    return CreateProtectionDomain();
}

bool RdmaManager::CreateProtectionDomain() {
    m_protectionDomain = ibv_alloc_pd(m_context);
    if (!m_protectionDomain) {
        std::cerr << "Failed to allocate protection domain" << std::endl;
        return false;
    }
    return true;
}

bool RdmaManager::RegisterMemory(void* ptr, size_t size) {
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

bool RdmaManager::Transfer(void* localBuffer, size_t bufferSize, 
                          uint64_t remoteAddr, uint32_t remoteKey) {
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
    
    // 执行RDMA写入操作
    ibv_sge sge;
    sge.addr = reinterpret_cast<uint64_t>(localBuffer);
    sge.length = bufferSize;
    sge.lkey = it->second->lkey;
    
    ibv_send_wr wr = {};
    wr.wr_id = 0;
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remoteAddr;
    wr.wr.rdma.rkey = remoteKey;
    
    ibv_send_wr* bad_wr = nullptr;
    if (ibv_post_send(qp, &wr, &bad_wr)) {
        std::cerr << "Failed to post RDMA write" << std::endl;
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

void RdmaManager::Cleanup() {
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
