#pragma once

#include <thread>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <chrono>
#include "global_memory.h"

class CoolingService {
public:
    static CoolingService& Instance() {
        static CoolingService instance;
        return instance;
    }

    void Start();
    void Stop();
    void RecordAccess(uintptr_t ptr);

    // 获取数据热度状态 (热/冷)
    bool isHotData(uintptr_t ptr) const;
    
    // 获取数据流动性（迁移次数）
    uint32_t getMobility(uintptr_t ptr) const;
    
    // 获取数据稳定性评分
    float getStability(uintptr_t ptr) const;
    
    // 获取NUMA节点标识符
    int getNumaId(uintptr_t ptr) const;
    
    // 获取数据热度值
    float getTemperature(uintptr_t ptr) const;

private:
    struct AccessRecord {
        uint64_t access_count = 0;
        std::chrono::steady_clock::time_point last_access;
        uint32_t mobility_count = 0;         // 迁移次数
        float stability_score = 0.0f;        // 稳定性评分 (0.0~1.0)
        int numaId = -1;                     // NUMA节点标识符
        float temperature = 0.0f;            // 数据热度值
    };

    void Run();

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::shared_mutex data_mutex_;
    std::unordered_map<uintptr_t, AccessRecord> access_records_;
    
    // 冷却参数 (默认值)
    std::chrono::seconds cooling_interval_{10};  // 冷却周期
    uint32_t decay_amount_{1};                   // 每次衰减值
    uint32_t access_threshold_{4};               // 访问阈值
    std::chrono::seconds access_window_{5};       // 访问时间窗口
};
