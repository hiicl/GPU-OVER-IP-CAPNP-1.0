@0xdabbadebb1a55e71;

using Common = import "common.capnp";
using Control = import "gpu-control.capnp";

struct AllocationResult {
  fakePtr @0 :UInt64;
  error @1 :Common.ErrorCode;
}

struct MemcpyPlan {
  targetServerIp @0 :Text;
  targetServerRdmaPort @1 :UInt32;
  remotePtr @2 :UInt64;
  error @3 :Common.ErrorCode;
}

struct NodeStatus {
  id @0 :Text;
  availableMemory @1 :UInt64;
  gpuUtilization @2 :Float32;
  networkLatency @3 :Float32;
}

interface HookLauncher {
  requestAllocation @0 (size :UInt64) -> (result :AllocationResult);
  requestFree @1 (fakePtr :UInt64) -> (error :Common.ErrorCode);

  planMemcpyHtoD @2 (dstFakePtr :UInt64, size :UInt64) -> (plan :MemcpyPlan);
  planMemcpyDtoH @3 (srcFakePtr :UInt64, size :UInt64) -> (plan :MemcpyPlan);

  launchKernel @4 (
    func :Text,
    gridDim :UInt32,
    blockDim :UInt32,
    sharedMem :UInt32,
    params :Data
  ) -> (error :Common.ErrorCode);

  getNodeStatus @5 () -> (nodes :List(NodeStatus));
}
