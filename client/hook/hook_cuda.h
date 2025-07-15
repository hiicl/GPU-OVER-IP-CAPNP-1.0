#pragma once
#include <optional>

#include <cuda.h>
#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <optional>
#include <iostream>

#include "context_manager.h"
#include "rdma_manager.h"

// 定义函数指针类型
typedef CUresult (CUDAAPI *cuMemAlloc_t)(CUdeviceptr* dptr, size_t bytesize);
typedef CUresult (CUDAAPI *cuMemFree_t)(CUdeviceptr dptr);
typedef CUresult (CUDAAPI *cuMemcpyHtoD_t)(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount);
typedef CUresult (CUDAAPI *cuMemcpyDtoH_t)(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount);
typedef CUresult (CUDAAPI *cuLaunchKernel_t)(
    CUfunction f,
    unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream hStream,
    void** kernelParams, void** extra);

// 声明全局函数指针为extern
extern cuMemAlloc_t pOriginal_cuMemAlloc;
extern cuMemFree_t pOriginal_cuMemFree;
extern cuMemcpyHtoD_t pOriginal_cuMemcpyHtoD;
extern cuMemcpyDtoH_t pOriginal_cuMemcpyDtoH;
extern cuLaunchKernel_t pOriginal_cuLaunchKernel;

// Hook函数声明
CUresult __stdcall Hooked_cuMemAlloc(CUdeviceptr* dev_ptr, size_t byte_size);
CUresult __stdcall Hooked_cuMemFree(CUdeviceptr dptr);
CUresult __stdcall Hooked_cuMemcpyHtoD(CUdeviceptr dstDevice, const void* srcHost, size_t ByteCount);
CUresult __stdcall Hooked_cuMemcpyDtoH(void* dstHost, CUdeviceptr srcDevice, size_t ByteCount);
CUresult __stdcall Hooked_cuLaunchKernel(CUfunction f,
    unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ,
    unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ,
    unsigned int sharedMemBytes, CUstream hStream, void** kernelParams, void** extra);
