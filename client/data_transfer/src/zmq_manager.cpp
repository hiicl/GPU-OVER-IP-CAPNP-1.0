#include "data_transfer.h"
#include <zmq.h>
#include <string>
#include <stdexcept>

// ZMQ 零拷贝回调函数
static void zmq_free_fn(void* /*data*/, void* /*hint*/) {}

DATATRANSFER_API bool SendData(const char* ip, unsigned short port, const void* buffer, size_t size) {
    // 创建 ZMQ 上下文
    void* context = zmq_ctx_new();
    if (!context) return false;
    
    // 创建 ZMQ 套接字
    void* socket = zmq_socket(context, ZMQ_DGRAM);
    if (!socket) {
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 构建目标端点
    std::string endpoint = "udp://" + std::string(ip) + ":" + std::to_string(port);
    
    // 连接到目标端点
    if (zmq_connect(socket, endpoint.c_str()) != 0) {
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 初始化消息（零拷贝）
    zmq_msg_t msg;
    if (zmq_msg_init_data(&msg, const_cast<void*>(buffer), size, zmq_free_fn, nullptr) != 0) {
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 发送消息
    int sent = zmq_msg_send(&msg, socket, 0);
    zmq_msg_close(&msg);
    zmq_close(socket);
    zmq_ctx_destroy(context);
    
    return sent != -1;
}

DATATRANSFER_API bool ReceiveData(const char* ip, unsigned short port, void* buffer, size_t size) {
    // 创建 ZMQ 上下文
    void* context = zmq_ctx_new();
    if (!context) return false;
    
    // 创建 ZMQ 套接字
    void* socket = zmq_socket(context, ZMQ_DGRAM);
    if (!socket) {
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 构建监听端点
    std::string endpoint = "udp://" + std::string(ip) + ":" + std::to_string(port);
    
    // 绑定到监听端点
    if (zmq_bind(socket, endpoint.c_str()) != 0) {
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 初始化消息
    zmq_msg_t msg;
    if (zmq_msg_init(&msg) != 0) {
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 接收消息
    int received = zmq_msg_recv(&msg, socket, 0);
    if (received == -1) {
        zmq_msg_close(&msg);
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 检查消息大小
    size_t msg_size = zmq_msg_size(&msg);
    if (msg_size != size) {
        zmq_msg_close(&msg);
        zmq_close(socket);
        zmq_ctx_destroy(context);
        return false;
    }
    
    // 复制数据到缓冲区
    memcpy(buffer, zmq_msg_data(&msg), size);
    zmq_msg_close(&msg);
    zmq_close(socket);
    zmq_ctx_destroy(context);
    
    return true;
}
