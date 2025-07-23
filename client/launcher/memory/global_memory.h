#pragma once

#include <string>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <chrono>
#include "launcher_client.h"

struct RemoteAllocInfo {
    std::string node_id;
    size_t size;
    uint64_t remote_handle;
    uint32_t access_count = 0;                           // 访问计数器
    std::chrono::steady_clock::time_point last_access;   // 最后访问时间
};

// 全局内存服务类
class GlobalMemoryService {
    std::unordered_map<uintptr_t, RemoteAllocInfo> ptr_table;
    std::shared_mutex table_mutex;

public:
    void AddMapping(uintptr_t dptr, RemoteAllocInfo info);
    std::optional<RemoteAllocInfo> GetMapping(uintptr_t dptr);
    void RemoveMapping(uintptr_t dptr);
    void SaveSnapshot(const std::string& path);
    
    // 获取所有内存指针
    std::vector<uintptr_t> GetAllPointers() {
        std::shared_lock<std::shared_mutex> lock(table_mutex);
        std::vector<uintptr_t> pointers;
        pointers.reserve(ptr_table.size());
        for (const auto& pair : ptr_table) {
            pointers.push_back(pair.first);
        }
        return pointers;
    }
};
