#pragma once

// 数据转移模块接口定义
#ifdef DATATRANSFER_EXPORTS
#define DATATRANSFER_API __declspec(dllexport)
#else
#define DATATRANSFER_API __declspec(dllimport)
#endif

#include <cstddef>

extern "C" {
    // 零拷贝数据发送接口
    DATATRANSFER_API bool SendData(const char* ip, 
                                  unsigned short port,
                                  const void* buffer, 
                                  size_t size);
    
    // 零拷贝数据接收接口
    DATATRANSFER_API bool ReceiveData(const char* ip, 
                                     unsigned short port,
                                     void* buffer, 
                                     size_t size);
}
