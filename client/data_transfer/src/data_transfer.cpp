#include "data_transfer.h"

// 数据传输模块入口实现
DATATRANSFER_API bool SendData(const char* ip, unsigned short port, 
                              const void* buffer, size_t size) {
    // 实际实现在 zmq_manager.cpp 中
    return ::SendData(ip, port, buffer, size);
}

DATATRANSFER_API bool ReceiveData(const char* ip, unsigned short port,
                                 void* buffer, size_t size) {
    // 实际实现在 zmq_manager.cpp 中
    return ::ReceiveData(ip, port, buffer, size);
}
