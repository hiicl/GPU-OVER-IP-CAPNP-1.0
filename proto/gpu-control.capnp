@0xcccca1cefaaf9876;

using Common = import "common.capnp";

# ===== GPU 资源状态结构 =====

struct GpuList {
    gpus @0 :List(Common.GpuInfo);
}

struct GpuRequest {
    uuid @0 :Common.UUID;
}

struct GpuStatus {
    usedMemory @0 :Int64;
    utilization @1 :Int32;
}

struct Ack {
    ok @0 :Bool;
    msg @1 :Text;
    code @2 :Common.ErrorCode;
}

struct RunRequest {
    uuid @0 :Common.UUID;
    cmd @1 :Text;
    streamHandle @2 :Common.Handle;
}

struct RunResponse {
    exitCode @0 :Int32;
    output @1 :Text;
}

# ===== 拓扑路径结构 =====

struct Path {
    type @0 :PathType;
    steps @1 :List(Step);
    bandwidth @2 :Float32;
}

struct Step {
    device @0 :Common.UUID;
    memType @1 :MemType;
    numaNode @2 :UInt32;
}

enum PathType {
    nvlink @0;
    xbus @1;
    roce @2;
}

enum MemType {
    device @0;
    host @1;
    unified @2;
}

struct Metrics {
    throughput @0 :Float32;
    latency @1 :Float32;
    errorRate @2 :Float32;
}

# ===== 控制接口 =====

interface Scheduler {
    # 调度器接口
    requestPath @0 (src :Common.UUID, dst :Common.UUID) -> (path :Path);
    reportMetrics @1 (metrics :Metrics) -> ();
    registerGpu @2 (info :Common.GpuInfo) -> (success :Bool);

    # 原 GpuService 接口
    listGpus @3 () -> (gpus :GpuList);
    getGpuStatus @4 (request :GpuRequest) -> (status :GpuStatus);
    acquireGpu @5 (request :GpuRequest) -> (ack :Ack);
    releaseGpu @6 (request :GpuRequest) -> (ack :Ack);
    runCommand @7 (request :RunRequest) -> (response :RunResponse);
}
