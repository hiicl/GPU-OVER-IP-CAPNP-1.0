#include "context_manager.h"
#include "hook_cuda.h"
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <nlohmann/json.hpp>
#include <optional>

using json = nlohmann::json;

void MemoryMapper::AddMapping(CUdeviceptr dptr, RemoteAllocInfo info) {
    std::unique_lock<std::shared_mutex> lock(table_mutex);
    ptr_table[dptr] = info;
}

std::optional<RemoteAllocInfo> MemoryMapper::GetMapping(CUdeviceptr dptr) {
    std::shared_lock<std::shared_mutex> lock(table_mutex);
    auto it = ptr_table.find(dptr);
    return it != ptr_table.end() ? std::optional<RemoteAllocInfo>(it->second) : std::nullopt;
}

void MemoryMapper::RemoveMapping(CUdeviceptr dptr) {
    std::unique_lock<std::shared_mutex> lock(table_mutex);
    ptr_table.erase(dptr);
}

void MemoryMapper::SaveSnapshot(const std::string& path) {
    std::shared_lock<std::shared_mutex> lock(table_mutex);
    json j;
    for (const auto& entry : ptr_table) {
        j[std::to_string((uintptr_t)entry.first)] = {
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
