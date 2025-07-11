@0xcccca1cefaaf9876;

using Common = import "common.capnp"; # 基础类型定义

struct GpuList {
    gpus @0 :List(Common.GpuInfo);
} # GPU信息列表

struct GpuRequest {
    uuid @0 :Common.UUID; # 目标GPU的UUID
}

struct GpuStatus {
    usedMemory @0 :Int64;    # 已用显存(字节)
    utilization @1 :Int32;   # GPU利用率(百分比)
}

struct Ack {
    ok @0 :Bool;             # 操作是否成功
    msg @1 :Text;            # 附加消息
    code @2 :Common.ErrorCode; # 错误代码
} # 通用响应结构

struct RunRequest {
    uuid @0 :Common.UUID;    # 目标GPU的UUID
    cmd @1 :Text;            # 要执行的命令
    streamHandle @2 :Common.Handle; # 关联的流句柄
} # 命令执行请求

struct RunResponse {
    exitCode @0 :Int32;      # 命令退出码
    output @1 :Text;         # 命令输出
} # 命令执行响应

struct Path {
    type @0 :PathType;       # 路径类型(NVLink/XBus/RoCE)
    steps @1 :List(Step);    # 路径步骤
    bandwidth @2 :Float32;   # 路径带宽(MB/s)
} # 设备间通信路径

struct Step {
    device @0 :Common.UUID;  # 设备UUID
    memType @1 :MemType;     # 内存类型
    numaNode @2 :UInt32;     # NUMA节点ID
} # 路径步骤定义

enum PathType {
    nvlink @0;  # NVLink连接
    xbus @1;    # PCIe XBus连接
    network @2; # 网卡连接
} # 路径类型枚举

enum MemType {
    device @0;  # 设备内存
    host @1;    # 主机内存
    unified @2; # 统一内存
} # 内存类型枚举

struct Metrics {
    throughput @0 :Float32;  # 吞吐量(MB/s)
    latency @1 :Float32;     # 延迟(ms)
    errorRate @2 :Float32;   # 错误率(%)
} # 性能指标结构

interface Scheduler {
    requestPath @0 (src :Common.UUID, dst :Common.UUID) -> (path :Path); # 请求设备间通信路径
    reportMetrics @1 (metrics :Metrics) -> (); # 上报性能指标
    registerGpu @2 (info :Common.GpuInfo) -> (success :Bool); # 注册GPU设备
    
    listGpus @3 () -> (gpus :GpuList); # 列出所有GPU信息
    getGpuStatus @4 (request :GpuRequest) -> (status :GpuStatus); # 获取GPU状态
    acquireGpu @5 (request :GpuRequest) -> (ack :Ack); # 申请GPU资源
    releaseGpu @6 (request :GpuRequest) -> (ack :Ack); # 释放GPU资源
    runCommand @7 (request :RunRequest) -> (response :RunResponse); # 在GPU上执行命令
} # GPU资源调度接口
