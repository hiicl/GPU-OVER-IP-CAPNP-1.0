@0xdabbadebb1a55e71;
# @file hook-launcher.capnp
# @brief 提供跨设备协调服务
# 
# 职责范围：
# - 跨节点内存传输规划
# - 分布式系统状态监控
# - 高级协调功能
using Common = import "common.capnp"; # 基础类型定义
using Kernel = import "kernel.capnp"; # 内核操作定义

struct MemcpyPlan {
  targetServerIp @0 :Text;
  targetServerZmqPort @1 :UInt16;
  error @2 :Common.ErrorCode;
}

struct NodeStatus {
  id @0 :Text;
  availableMemory @1 :UInt64;
  gpuUtilization @2 :Float32;
  networkLatency @3 :Float32;
}

interface HookLauncher {
  planMemcpyHtoD @0 (dstHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan); # 规划主机到设备的内存传输
  
  planMemcpyDtoH @1 (srcHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan); # 规划设备到主机的内存传输
  
  getNodeStatus @2 () -> (nodes :List(NodeStatus)); # 获取分布式节点状态
  
  prefetchData @3 (handle: Common.MemoryHandle) -> (ack: Common.Ack); # 预取数据到目标设备
  
  measureBandwidth @4 (src: Common.UUID, dst: Common.UUID) -> (result: BandwidthResult); # 测量节点间带宽
  
  trackAsyncTask @5 (taskId: UInt64) -> (status: TaskStatus); # 跟踪异步任务状态
}

# ===== 新增结构定义 =====
struct BandwidthResult {
    throughput @0 :Float32;  # 传输速率 (MB/s)
    latency @1 :Float32;     # 延迟 (ms)
}

struct TaskStatus {
    progress @0 :UInt8;      # 进度百分比 (0-100)
    estimatedTime @1 :UInt32; # 预计剩余时间 (ms)
}
