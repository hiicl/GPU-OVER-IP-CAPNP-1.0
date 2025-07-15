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

// Third-party libraries
#include <nlohmann/json.hpp>
#include <zmq.h>

// Cap'n Proto & KJ
#include <capnp/ez-rpc.h>
#include <kj/async-io.h>

// CUDA
#include <cuda.h>

// Project-specific generated headers
// Ensure the path is correct for your build setup
#include "common.capnp.h"
#include "cuda.capnp.h"
#include "hook-launcher.capnp.h"
#endif //PCH_H
