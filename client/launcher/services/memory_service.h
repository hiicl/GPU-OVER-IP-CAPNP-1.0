#pragma once
#include "global_memory.h"
#include "hook-launcher.capnp.h"
#include <capnp/capnp.h>

class MemoryService final : public GenericServices::Server {
public:
    MemoryService(GlobalMemoryManager& memoryManager);

    // 内存分配
    ::kj::Promise<void> allocateMemory(AllocateMemoryContext context) override;
    
    // 内存释放
    ::kj::Promise<void> freeMemory(FreeMemoryContext context) override;

private:
    GlobalMemoryManager& memoryManager_;
};
