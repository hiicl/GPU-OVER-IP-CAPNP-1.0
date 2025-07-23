#ifndef PLANK_TRANSPORT_H
#define PLANK_TRANSPORT_H

#include <cstdint>
#include <vector>
#include <memory>

class PlankTransport {
public:
    virtual ~PlankTransport() = default;
    
    // 发送数据到目标节点（使用跳板机制）
    virtual bool sendData(uint64_t src_handle, uint64_t dst_handle, 
                         const void* data, size_t size, int src_node, int dst_node) = 0;
                         
    // 从源节点接收数据（使用跳板机制）
    virtual bool receiveData(uint64_t src_handle, uint64_t dst_handle,
                            void* buffer, size_t size, int src_node, int dst_node) = 0;
    
    // 跨节点传输GPU缓冲区（GDR-to-GDR）
    virtual bool transferGpuBuffer(uint64_t src_gpu_handle, uint64_t dst_gpu_handle,
                                  size_t size, int src_node, int dst_node) = 0;
};

// 创建跳板传输实例
std::unique_ptr<PlankTransport> createPlankTransport();

#endif // PLANK_TRANSPORT_H
