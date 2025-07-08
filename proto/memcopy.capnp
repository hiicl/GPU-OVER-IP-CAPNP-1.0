@0x9876543210abcdef;

struct MemcopyCommand {
  opType     @0 :UInt8;   # 操作类型：1=memcpy, 2=memset, 3=host-read
  dstAddress @1 :UInt64;  # 远程内存地址（设备或主机映射）
  dataSize   @2 :UInt32;  # 有效数据长度
  value      @3 :UInt8;   # memset 使用的值（当 opType = 2 时有效）
  data       @4 :Data;    # 需要复制的有效数据（memcpy 有效）
  checksum   @5 :UInt32;  # 可选 CRC32，用于数据一致性校验
}
