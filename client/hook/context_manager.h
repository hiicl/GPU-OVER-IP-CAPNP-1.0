#pragma once

#include <cuda.h>
#include <string>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

// 远程分配信息结构
struct RemoteAllocInfo {
    std::string node_id;
    size_t size;
    uintptr_t remote_handle;
};

// 内存映射表管理类
class MemoryMapper {
    std::unordered_map<CUdeviceptr, RemoteAllocInfo> ptr_table;
    std::shared_mutex table_mutex;

public:
    void AddMapping(CUdeviceptr dptr, RemoteAllocInfo info);
    std::optional<RemoteAllocInfo> GetMapping(CUdeviceptr dptr);
    void RemoveMapping(CUdeviceptr dptr);
    void SaveSnapshot(const std::string& path);
};
