#pragma once

#include <infiniband/verbs.h>
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>

class RdmaManager {
public:
    RdmaManager();
    ~RdmaManager();

    // 初始化RDMA环境，指定NUMA节点
    bool Initialize(int numa_node);

    // 注册内存区域（用于GPUDirect RDMA）
    bool RegisterMemory(void* ptr, size_t size);

    // 执行RDMA传输
    bool Transfer(void* localBuffer, size_t bufferSize, uint64_t remoteAddr, uint32_t remoteKey);

private:
    struct ibv_context* m_context = nullptr;
    struct ibv_pd* m_protectionDomain = nullptr;
    int m_numaNode = -1;
    std::mutex m_mutex;

    // 内存区域映射
    std::unordered_map<void*, struct ibv_mr*> m_memoryRegions;

    // 创建保护域
    bool CreateProtectionDomain();

    // 销毁资源
    void Cleanup();
};
