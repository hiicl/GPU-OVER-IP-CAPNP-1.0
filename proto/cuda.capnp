@0xbbbbcccc22224444;

using Common = import "common.capnp";
using Control = import "gpu-control.capnp";

struct CudaMemInfo {
    addr @0 :Common.Handle;
    size @1 :UInt64;
}

struct MemcpyParams {
    src @0 :Common.Handle;
    dst @1 :Common.Handle;
    size @2 :UInt64;
    direction @3 :Direction;
}

enum Direction {
    hostToDevice @0;
    deviceToHost @1;
    deviceToDevice @2;
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

struct BatchRunRequest {
    requests @0 :List(Control.RunRequest);
    stream @1 :Common.Handle;
}

struct BatchRunResponse {
    responses @0 :List(Control.RunResponse);
}

struct MultiGpuRequest {
    uuids @0 :List(Common.UUID);
    command @1 :Text;
    stream @2 :Common.Handle;
}

interface CudaService {
    cudaInit @0 () -> (ack :Control.Ack);
    cudaMemAlloc @1 (info :CudaMemInfo) -> (result :CudaMemInfo);
    cudaMemcpy @2 (params :MemcpyParams) -> (ack :Control.Ack);
    cudaMemFree @3 (info :CudaMemInfo) -> (ack :Control.Ack);

    createCudaStream @4 (params :StreamCreateParams) -> (handle :StreamHandle);
    destroyCudaStream @5 (handle :StreamHandle) -> (ack :Control.Ack);
    synchronizeCudaStream @6 (handle :StreamHandle) -> (ack :Control.Ack);

    cudaKernelLaunch @7 (request :Control.RunRequest) -> (response :Control.RunResponse);

    createEvent @8 (params :EventParams) -> (handle :EventHandle);
    recordEvent @9 (handle :EventHandle) -> (ack :Control.Ack);
    eventSynchronize @10 (handle :EventHandle) -> (ack :Control.Ack);
    destroyEvent @11 (handle :EventHandle) -> (ack :Control.Ack);

    batchKernelLaunch @12 (request :BatchRunRequest) -> (response :BatchRunResponse);
    multiGpuCooperation @13 (request :MultiGpuRequest) -> (ack :Control.Ack);
}
