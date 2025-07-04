@0xdabbadebb1a55e71;

using Common = import "common.capnp";
using Kernel = import "kernel.capnp";

struct AllocationResult {
  handle @0 :Common.MemoryHandle;  # 使用统一内存句柄
  error @1 :Common.ErrorCode;
}

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
  requestAllocation @0 (size :UInt64) -> (result :AllocationResult);
  requestFree @1 (handle :Common.MemoryHandle) -> (error :Common.ErrorCode);

  planMemcpyHtoD @2 (dstHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan);
  planMemcpyDtoH @3 (srcHandle :Common.MemoryHandle, size :UInt64) -> (plan :MemcpyPlan);

  launchKernel @4 (request :Kernel.KernelLaunch) -> (error :Common.ErrorCode);  # 使用统一内核请求

  getNodeStatus @5 () -> (nodes :List(NodeStatus));
}
