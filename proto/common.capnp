@0xdefdefdefdefdef0;

# 基础类型定义
using UUID = Text;
using Handle = UInt64;

struct ID {
  union {
    uuid @0 :Text;   # UUID格式
    handle @1 :UInt64; # 数值句柄
  }
}

struct Ack {
  ok @0 :Bool;
  msg @1 :Text;
  code @2 :ErrorCode;
}

struct GpuInfo {
    id @0 :ID;
    name @1 :Text;
    totalMemory @2 :Int64;
    metadata @3 :Metadata; # 扩展元数据
}

struct Metadata {
    numaAffinity @0 :Int32 = -1; # NUMA亲和性
    gdrSupport @1 :Bool = false; # GDR支持
    # 可扩展其他元数据字段
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
    id @0 :ID;      # 使用统一ID类型
    size @1 :UInt64; # 内存大小(字节)
    nodeId @2 :UInt32; # 所在节点ID
}

enum TransferDirection {
    htod @0;  # 主机到设备
    dtoh @1;  # 设备到主机
    dtod @2;  # 设备到设备
    gdr @3;   # GPU直接访问(GDR)
}

struct Response {
    success @0 :Bool;
    data @1 :Data;  # 实际返回数据
    error @2 :ErrorCode; # 错误代码
    message @3 :Text; # 消息
} # 统一响应结构
