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
