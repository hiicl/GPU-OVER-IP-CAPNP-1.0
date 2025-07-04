@0xbbbbcccc22224444;

using Common = import "common.capnp";
using Kernel = import "kernel.capnp";

struct CudaMemInfo {
    handle @0 :Common.MemoryHandle;
    size @1 :UInt64;
}

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
    cudaInit @0 () -> (ack :Common.Ack);
    cudaMemAlloc @1 (size :UInt64) -> (result :CudaMemInfo);
    cudaMemcpy @2 (params :MemcpyParams) -> (ack :Common.Ack);
    cudaMemFree @3 (handle :Common.MemoryHandle) -> (ack :Common.Ack);

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
}
