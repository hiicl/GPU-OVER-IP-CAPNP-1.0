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
    localPtr @0 :UInt64;    # 客户端虚拟指针
    remoteHandle @1 :UInt64; # 服务端物理句柄
    nodeId @2 :UInt32;       # 所在节点ID
}

enum TransferDirection {
    htod @0;  # 主机到设备
    dtoh @1;  # 设备到主机
    dtod @2;  # 设备到设备
}
