@0xdefdefdefdefdef0;

using UUID = Text;
using Handle = UInt64;

struct GpuInfo {
    uuid @0 :UUID;
    name @1 :Text;
    totalMemory @2 :Int64;
}

enum ErrorCode {
    ok @0;
    outOfMemory @1;
    gpuNotFound @2;
    streamError @3;
    kernelLaunchFail @4;
    unknown @5;
}

struct MemoryHandle {
    id @0 :UInt64;   # 全局唯一资源ID
    size @1 :UInt64; # 内存大小(字节)
    nodeId @2 :UInt32; # 所在节点ID
}

enum TransferDirection {
    htod @0;  # 主机到设备
    dtoh @1;  # 设备到主机
    dtod @2;  # 设备到设备
}

struct Ack {
    success @0 :Bool = false;
    errorCode @1 :ErrorCode;
    message @2 :Text;
} # 错误响应结构

struct Result {
    success @0 :Bool = true;
    data @1 :Data;  # 实际返回数据
} # 成功响应结构
