#include "memory_service.h"
#include <kj/debug.h>

MemoryService::MemoryService(GlobalMemoryManager& memoryManager)
    : memoryManager_(memoryManager) {}

kj::Promise<void> MemoryService::allocateMemory(AllocateMemoryContext context) {
    auto params = context.getParams();
    size_t size = params.getSize();
    uint32_t numaHint = params.getNumaHint();
    
    try {
        auto allocation = memoryManager_.allocate(size, numaHint);
        
        auto results = context.getResults();
        results.setHandle(allocation.handle);
        results.setLocalPtr(reinterpret_cast<uint64_t>(allocation.localPtr));
        results.setNumaNode(allocation.numaNode);
        
        KJ_LOG(INFO, "Allocated memory", size, "bytes on NUMA", allocation.numaNode);
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Memory allocation failed", e.what());
        auto results = context.getResults();
        results.setError(Common::ErrorCode::OUT_OF_MEMORY);
    }
    
    return kj::READY_NOW;
}

kj::Promise<void> MemoryService::freeMemory(FreeMemoryContext context) {
    auto params = context.getParams();
    uint64_t remoteHandle = params.getRemoteHandle();
    
    try {
        memoryManager_.free(remoteHandle);
        KJ_LOG(INFO, "Freed memory", remoteHandle);
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Memory free failed", e.what());
        auto results = context.getResults();
        results.setError(Common::ErrorCode::INVALID_HANDLE);
    }
    
    return kj::READY_NOW;
}
