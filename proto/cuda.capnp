@0xbbbbcccc22224444;
# @file cuda.capnp
# @brief 提供单设备CUDA操作接口
# 
# 职责范围：
# - 显存管理（分配/释放/复制）
# - CUDA流和事件管理
# - 内核启动与执行
# - 多GPU协作
using Common = import "common.capnp"; # 基础类型定义
using Kernel = import "kernel.capnp"; # 内核操作定义

struct MemcpyParams {
    src @0 :Common.MemoryHandle;
    dst @1 :Common.MemoryHandle;
    size @2 :UInt64;
    direction @3 :Common.TransferDirection;
}

struct StreamHandle {
    handle @0 :Common.Handle;
}

struct StreamCreateParams {
    flags @0 :UInt32;
}

struct EventHandle {
    handle @0 :Common.Handle;
}

struct EventParams {
    flags @0 :UInt32;
}

struct BatchKernelLaunch {
    requests @0 :List(Kernel.KernelLaunch);
    stream @1 :Common.Handle;
}

struct MultiGpuRequest {
    uuids @0 :List(Common.UUID);
    command @1 :Text;
    stream @2 :Common.Handle;
}

interface CudaService {
    cudaInit @0 () -> (ack :Common.Ack); # 初始化CUDA环境
    
    cudaMemAlloc @1 (size :UInt64) -> (result :Common.Result); # 分配显存
    
    cudaMemcpy @2 (params :MemcpyParams) -> (ack :Common.Ack); # 执行内存复制
    
    cudaMemFree @3 (handle :Common.MemoryHandle) -> (ack :Common.Ack); # 释放显存

    createCudaStream @4 (params :StreamCreateParams) -> (handle :StreamHandle);
    destroyCudaStream @5 (handle :StreamHandle) -> (ack :Common.Ack);
    synchronizeCudaStream @6 (handle :StreamHandle) -> (ack :Common.Ack);

    cudaKernelLaunch @7 (request :Kernel.KernelLaunch) -> (ack :Common.Ack);

    createEvent @8 (params :EventParams) -> (handle :EventHandle);
    recordEvent @9 (handle :EventHandle) -> (ack :Common.Ack);
    eventSynchronize @10 (handle :EventHandle) -> (ack :Common.Ack);
    destroyEvent @11 (handle :EventHandle) -> (ack :Common.Ack);

    batchKernelLaunch @12 (request :BatchKernelLaunch) -> (ack :Common.Ack);
    multiGpuCooperation @13 (request :MultiGpuRequest) -> (ack :Common.Ack);
    
    allocAndWrite @14 (size :UInt64, data :Data) -> (result :Common.Result); # 分配显存并写入数据
}
