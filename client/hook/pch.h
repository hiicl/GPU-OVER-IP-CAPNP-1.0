// pch.h: 这是预编译标头文件。
// 下方列出的文件仅编译一次，提高了将来生成的生成性能。
// 这还将影响 IntelliSense 性能，包括代码完成和许多代码浏览功能。
// 但是，如果此处列出的文件中的任何一个在生成之间有更新，它们全部都将被重新编译。
// 请勿在此处添加要频繁更新的文件，这将使得性能优势无效。

#ifndef PCH_H
#define PCH_H

// 添加要在此处预编译的标头
#include "framework.h"
// Standard C++ Library
#include <cstring>  // 添加标准字符串函数头文件
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <map>
#include <fstream>

// Windows Headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h> // For USHORT

// ==================================================
// 纯C头文件 (使用extern "C"包裹)
// ==================================================
#ifdef __cplusplus
extern "C" {
#endif

#include <zmq.h>
#include <easyhook.h>

#ifdef __cplusplus
}
#endif

// ==================================================
// C++头文件 (模板/类等)
// ==================================================
// 标准库
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <map>
#include <fstream>
#include <stdexcept>

// 第三方C++库
#include <nlohmann/json.hpp>

// Cap'n Proto & KJ (带命名空间保护)
#if defined(_MSC_VER)
#pragma push_macro("_CRT_MEMORY_DEFINED")
#undef _CRT_MEMORY_DEFINED
#endif

#include <capnp/ez-rpc.h>
#include <kj/async-io.h>

#if defined(_MSC_VER)
#pragma pop_macro("_CRT_MEMORY_DEFINED")
#endif

// CUDA
#include <cuda.h>

// 项目生成头文件
#include "common.capnp.h"
#include "cuda.capnp.h"
#include "hook-launcher.capnp.h"

// 项目自定义头文件
#include "launcher_client.h"
#include "data_transfer.h"
#include "hook_cuda.h"
#endif //PCH_H
