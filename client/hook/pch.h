/*
 * =====================================================================================
 *
 * Filename:  pch.h
 *
 * Description:  Precompiled header for the cuda-hook project.
 *
 * FIX: This file is now correctly structured to separate C++ and C headers.
 * All .cpp files should include this header FIRST, and ONLY this header.
 *
 * =====================================================================================
 */
#pragma once

// =======================================================================
// SECTION 1: C++ Headers (MUST be outside extern "C")
// =======================================================================

// Standard C Library Headers (essential for memcpy, memchr, etc.)
#include <cstring>  // For memcpy, memchr, memset, etc.
#include <cstdlib>  // For general C standard library functions
#include <cstdio>   // For C I/O functions

// Standard C++ Library
#include <iostream>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <map>
#include <fstream>
#include <stdexcept>

// Windows Headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h> // For USHORT

// Third-party C++ libraries
#include <nlohmann/json.hpp>
#include <capnp/ez-rpc.h>
#include <kj/async.h>

// CUDA API Header
#include <cuda.h>

// Project-specific generated C++ protocol headers
#include "common.capnp.h"
#include "cuda.capnp.h"
#include "hook-launcher.capnp.h"

// Project-specific C++ class headers
#include "../client/hook/launcher_client.h"
#include "../client/data_transfer/include/data_transfer.h"
#include "../client/hook/hook_cuda.h"

// =======================================================================
// SECTION 2: C-Linkage Headers and Declarations
// =======================================================================
#ifdef __cplusplus
extern "C" {
#endif

	// Third-party C libraries
#include <easyhook.h>
#include <zmq.h>

#ifdef __cplusplus
}
#endif
