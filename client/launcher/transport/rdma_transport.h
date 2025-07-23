#pragma once

#include <infiniband/verbs.h>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

// 传输类型定义
enum TransferType {
    STANDARD = 0,
    GDR = 1
};

class RdmaTransport {
public:
    RdmaTransport();
    ~RdmaTransport();

    // 初始化RDMA环境
    bool Initialize();

    // 注册内存区域
    bool RegisterMemory(void* ptr, size_t size);

    // 执行RDMA传输 (支持GDR-to-GDR)
    bool Transfer(void* localBuffer, size_t bufferSize, 
                  uint64_t remoteAddr, uint32_t remoteKey, 
                  TransferType type = STANDARD);

private:
    struct ibv_context* m_context = nullptr;
    struct ibv_pd* m_protectionDomain = nullptr;
    std::mutex m_mutex;

    // 内存区域映射
    std::unordered_map<void*, struct ibv_mr*> m_memoryRegions;

    // 创建保护域
    bool CreateProtectionDomain();
    
    // 启用GPU直接RDMA支持
    bool EnableGdr(ibv_qp* qp);

    // 销毁资源
    void Cleanup();
};
