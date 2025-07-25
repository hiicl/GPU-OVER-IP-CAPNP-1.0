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
using Memcopy = import "memcopy.capnp"; # 内存操作定义

struct MemcpyPlan {
  targetServerIp @0 :Text;
  targetServerZmqPort @1 :UInt16;
  error @2 :Common.ErrorCode;
}

struct NodeStatus {
  id @0 :Text;
  availableMemory @1 :UInt64;      # 可用内存
  gpuUtilization @2 :Float32;      # GPU利用率
  networkLatency @3 :Float32;       # 网络延迟
  numaNode @4 :UInt32;              # NUMA节点ID (新增)
  gpuCount @5 :UInt32;              # GPU数量 (新增)
  rdmaSupport @6 :Bool;             # RDMA支持 (新增)
}

# ===== 新增分配决策结构 =====
struct AllocationPlan {
  targetNodeId @0 :UInt32;          # 目标节点ID
  memoryType @1 :MemoryType;        # 内存类型 (VRAM/HOST)
  transportType @2 :TransportType;  # 传输协议 (RDMA/UDP/TCP)
  prefetchHint @3 :Bool;            # 是否预取
}

enum MemoryType {
  vram @0;
  host @1;
}

enum TransportType {
  rdma @0;
  udp @1;
  tcp @2;
}

interface HookLauncher {
  # ===== 分配决策接口 =====
  requestAllocationPlan @9 (size :UInt64) -> (plan :AllocationPlan); # 显存分配决策
  requestFreePlan @10 (fakePtr :UInt64) -> (ack :Common.Ack); # 释放内存
  
  # ===== 原有接口 =====
  planMemcpyHtoD @0 (dstHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan); # 规划主机到设备的内存传输
  
  planMemcpyDtoH @1 (srcHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan); # 规划设备到主机的内存传输
  
  getNodeStatus @2 () -> (nodes :List(NodeStatus)); # 获取分布式节点状态（包含NUMA信息）
  
  prefetchData @3 (handle: Common.MemoryHandle) -> (ack: Common.Ack); # 预取数据到目标设备
  
  measureBandwidth @4 (src: Common.UUID, dst: Common.UUID) -> (result: BandwidthResult); # 测量节点间带宽
  
  trackAsyncTask @5 (taskId: UInt64) -> (status: TaskStatus); # 跟踪异步任务状态

  # ===== 高级功能接口 =====
  getMemoryLocation @6 (fakePtr :UInt64) -> (location :NodeInfo); # 查询内存位置
  
  advisePrefetch @7 (fakePtr :UInt64) -> (ack :Common.Ack); # 建议预取数据

  # ===== RDMA传输规划接口 =====
  requestRdmaPlan @8 (srcHandle :Common.MemoryHandle, dstHandle :Common.MemoryHandle, size :UInt64) 
    -> (plan :Memcopy.RdmaPlan);
  
  # ===== 内核启动接口 =====
  launchKernel @11 (func :Text,
                   gridDimX :UInt32, gridDimY :UInt32, gridDimZ :UInt32,
                   blockDimX :UInt32, blockDimY :UInt32, blockDimZ :UInt32,
                   sharedMemBytes :UInt32,
                   params :Data) -> (ack :Common.Ack); # 启动内核
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

# ===== 新增内存位置信息结构 =====
struct NodeInfo {
    nodeId @0 :UInt32;      # 节点ID
    memoryType @1 :Text;    # 内存类型(VRAM/DRAM)
    error @2 :Common.ErrorCode;
}
