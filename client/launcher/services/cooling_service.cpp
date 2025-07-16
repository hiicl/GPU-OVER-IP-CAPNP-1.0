#include "cooling_service.h"
#include <iostream>
#include <chrono>
#include <cmath> // 用于std::exp
#include "global_memory.h"

CoolingService::CoolingService() 
    : cooling_interval_(10), 
      decay_amount_(1),
      access_threshold_(4),
      access_window_(5) {}

CoolingService::~CoolingService() {
    Stop();
}

void CoolingService::Start() {
    if (running_) return;
    running_ = true;
    worker_ = std::thread(&CoolingService::Run, this);
    std::cout << "Cooling service started" << std::endl;
}

void CoolingService::Stop() {
    if (!running_) return;
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
    std::cout << "Cooling service stopped" << std::endl;
}

void CoolingService::RecordAccess(uintptr_t ptr) {
    std::unique_lock<std::shared_mutex> lock(data_mutex_);
    auto now = std::chrono::steady_clock::now();
    
    // 更新访问记录
    auto& record = access_records_[ptr];
    record.access_count++;
    record.last_access = now;
    
    // 获取NUMA节点（简化实现，实际应从全局内存服务获取）
    record.numaId = GlobalMemory::GetNumaNodeForPtr(ptr);
    
    // 计算瞬时热度（基于最近访问）
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - record.last_access).count();
    if (time_since_last > 0) {
        record.temperature = 1.0f / time_since_last;
    } else {
        record.temperature = 1.0f; // 最大值
    }
}

bool CoolingService::isHotData(uintptr_t ptr) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return false; // 没有访问记录，视为冷数据
    }
    
    const auto& record = it->second;
    return record.temperature > 0.8f; // 热度阈值
}

uint32_t CoolingService::getMobility(uintptr_t ptr) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return 0;
    }
    return it->second.mobility_count;
}

float CoolingService::getStability(uintptr_t ptr) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return 0.0f;
    }
    return it->second.stability_score;
}

int CoolingService::getNumaId(uintptr_t ptr) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return -1;
    }
    return it->second.numaId;
}

float CoolingService::getTemperature(uintptr_t ptr) const {
    std::shared_lock<std::shared_mutex> lock(data_mutex_);
    auto it = access_records_.find(ptr);
    if (it == access_records_.end()) {
        return 0.0f;
    }
    return it->second.temperature;
}

void CoolingService::Run() {
    while (running_) {
        std::this_thread::sleep_for(cooling_interval_);
        
        auto now = std::chrono::steady_clock::now();
        std::unique_lock<std::shared_mutex> lock(data_mutex_);
        
        // 清理过期的访问记录并更新指标
        for (auto it = access_records_.begin(); it != access_records_.end();) {
            auto& record = it->second;
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - record.last_access);
            
            // 1. 衰减访问计数
            if (duration > access_window_) {
                if (record.access_count > decay_amount_) {
                    record.access_count -= decay_amount_;
                } else {
                    it = access_records_.erase(it);
                    continue;
                }
            }
            
            // 2. 计算稳定性评分（增强版）
            // 基于访问频率、规律性和生存时间
            float frequency_factor = std::min(1.0f, record.access_count / 100.0f);
            float time_factor = 1.0f - std::exp(-duration.count() / 3600.0f); // 1小时半衰期
            float pattern_factor = 0.5f; // 简化版，实际应分析访问模式
            
            record.stability_score = frequency_factor * pattern_factor * time_factor;
            
            // 3. 更新温度（随时间衰减）
            auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(now - record.last_access).count();
            if (time_since_last > 0) {
                record.temperature = record.temperature * std::exp(-0.001 * time_since_last);
            }
            
            ++it;
        }
    }
}
