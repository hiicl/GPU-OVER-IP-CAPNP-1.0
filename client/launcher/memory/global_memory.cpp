#include "global_memory.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

using json = nlohmann::json;

void GlobalMemoryService::AddMapping(uintptr_t dptr, RemoteAllocInfo info) {
    std::unique_lock<std::shared_mutex> lock(table_mutex);
    info.access_count = 0;
    info.last_access = std::chrono::steady_clock::now();
    ptr_table[dptr] = info;
}

std::optional<RemoteAllocInfo> GlobalMemoryService::GetMapping(uintptr_t dptr) {
    std::shared_lock<std::shared_mutex> lock(table_mutex);
    auto it = ptr_table.find(dptr);
    if (it != ptr_table.end()) {
        // 更新访问计数和时间
        it->second.access_count++;
        it->second.last_access = std::chrono::steady_clock::now();
    }
    return it != ptr_table.end() ? std::optional<RemoteAllocInfo>(it->second) : std::nullopt;
}

void GlobalMemoryService::RemoveMapping(uintptr_t dptr) {
    std::unique_lock<std::shared_mutex> lock(table_mutex);
    ptr_table.erase(dptr);
}

void GlobalMemoryService::SaveSnapshot(const std::string& path) {
    std::shared_lock<std::shared_mutex> lock(table_mutex);
    json j;
    for (const auto& entry : ptr_table) {
        j[std::to_string(entry.first)] = {
            {"node_id", entry.second.node_id},
            {"size", entry.second.size},
            {"remote_handle", entry.second.remote_handle}
        };
    }
    
    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(4);
    } else {
        std::cerr << "Failed to save memory mapping snapshot" << std::endl;
    }
}
