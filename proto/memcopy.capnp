@0x9876543210abcdef;

struct MemcopyCommand { # 内存复制命令协议：跨节点内存操作指令
  opType     @0 :UInt8;   # 操作类型(1=内存复制, 2=内存设置, 3=主机读取)
  dstAddress @1 :UInt64;  # 目标内存地址(设备或主机映射)
  dataSize   @2 :UInt32;  # 有效数据长度(字节)
  value      @3 :UInt8;   # 内存设置操作值(仅opType=2时有效)
  data       @4 :Data;    # 操作数据负载(仅opType=1时有效)
  checksum   @5 :UInt32;  # CRC32校验和(数据完整性验证)
}
