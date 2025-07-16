@0x9876543210abcdef;
using Common = import "common.capnp";

struct RdmaEndpoint {
  ip @0 :Text;    # RDMA目标IP
  port @1 :UInt16; # RDMA端口
  gid @2 :Data;   # RoCE GID
  key @3 :UInt32; # RDMA访问密钥
}

enum OperationType {
  copy @0;   # 内存复制
  set @1;    # 内存设置
  read @2;   # 主机读取
  gdr @3;    # GDR直接传输
}

struct MemoryOp { # 统一内存操作协议
  type @0 :OperationType;  # 操作类型
  target @1 :Common.MemoryHandle;  # 目标内存描述符
  source @2 :Common.MemoryHandle;  # 源内存描述符（仅复制操作）
  value @3 :UInt8;         # 设置值（仅设置操作）
  data @4 :Data;           # 数据负载（仅复制操作）
  checksum @5 :UInt32;     # CRC32校验和
  endpoint @6 :RdmaEndpoint; # RDMA端点信息
  flags @7 :UInt32;        # 操作标志位
}
