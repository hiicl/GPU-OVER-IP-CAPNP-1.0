@0xeeeeffff11112222;
using Common = import "common.capnp";

enum ParamType {
    scalar @0;
    pointer @1;
    buffer @2;
}

struct KernelParam {
    type @0 :ParamType;       # 参数类型
    value @1 :Data;           # 参数值
    offset @2 :UInt32;        # 缓冲区偏移（buffer时使用）
    alignment @3 :UInt8 = 4;  # 对齐字节（默认4）
}

struct KernelLaunch {
    name @0 :Text;          # 内核函数名
    gridX @1 :UInt32;       # Grid维度X
    gridY @2 :UInt32;       # Grid维度Y
    gridZ @3 :UInt32;       # Grid维度Z
    blockX @4 :UInt32;      # Block维度X
    blockY @5 :UInt32;      # Block维度Y
    blockZ @6 :UInt32;      # Block维度Z
    sharedMem @7 :UInt32;   # 共享内存大小（字节）
    params @8 :List(KernelParam); # 参数列表
    streamHandle @9 :Common.Handle = 0; # 流句柄（可选）
    paramBytes @10 :UInt32 = 0;   # 参数 buffer 总大小
    funcHandle @11 :Common.Handle = 0; # 函数指针句柄
    launchFlags @12 :UInt32 = 0;  # 保留字段
}
