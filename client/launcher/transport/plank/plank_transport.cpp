#include "plank_transport.h"
#include "../rdma_transport.h"
#include "../zmq_transport.h"
#include <cuda_runtime.h>
#include <iostream>
#include <memory>

class PlankTransportImpl : public PlankTransport {
public:
    PlankTransportImpl() 
        : rdma_transport_(createRdmaTransport()),
          zmq_transport_(createZmqTransport()) {}
    
    bool sendData(uint64_t src_handle, uint64_t dst_handle, 
                 const void* data, size_t size, int src_node, int dst_node) override {
        // 对于冷数据使用ZMQ UDP传输
        return zmq_transport_->send(dst_node, data, size);
    }
    
    bool receiveData(uint64_t src_handle, uint64_t dst_handle,
                    void* buffer, size_t size, int src_node, int dst_node) override {
        // 对于冷数据使用ZMQ UDP接收
        return zmq_transport_->receive(src_node, buffer, size);
    }
    
    bool transferGpuBuffer(uint64_t src_gpu_handle, uint64_t dst_gpu_handle,
                          size_t size, int src_node, int dst_node) override {
        // 1. 源节点：通过UVM实现GPU-CPU零拷贝
        void* host_ptr = nullptr;
        cudaError_t err = cudaHostAlloc(&host_ptr, size, cudaHostAllocMapped);
        if (err != cudaSuccess) {
            std::cerr << "cudaHostAlloc failed: " << cudaGetErrorString(err) << std::endl;
            return false;
        }
        
        // 2. 从GPU复制数据到CPU缓冲区
        err = cudaMemcpy(host_ptr, reinterpret_cast<void*>(src_gpu_handle), size, cudaMemcpyDeviceToHost);
        if (err != cudaSuccess) {
            std::cerr << "cudaMemcpy DtoH failed: " << cudaGetErrorString(err) << std::endl;
            cudaFreeHost(host_ptr);
            return false;
        }
        
        // 3. 使用RDMA将数据发送到目标节点
        if (!rdma_transport_->send(dst_node, host_ptr, size)) {
            std::cerr << "RDMA transfer failed" << std::endl;
            cudaFreeHost(host_ptr);
            return false;
        }
        
        // 4. 目标节点：通过cudaMemcpyHtoD将数据复制到GPU
        // 注意：在实际应用中，这一步需要在目标节点上执行
        // 这里简化处理，实际需要RPC通知目标节点
        
        cudaFreeHost(host_ptr);
        return true;
    }

private:
    std::unique_ptr<RdmaTransport> rdma_transport_;
    std::unique_ptr<ZmqTransport> zmq_transport_;
};

std::unique_ptr<PlankTransport> createPlankTransport() {
    return std::make_unique<PlankTransportImpl>();
}
