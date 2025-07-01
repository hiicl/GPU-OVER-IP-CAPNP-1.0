#include "dispatcher.h"
#include "launcher_client.h"  // 更新头文件
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <kj/async.h>
#include <capnp/rpc.h>
#include <algorithm>

// 计算节点综合得分
double CalculateNodeScore(const RemoteNode& node, size_t required_memory) {
    // 权重配置 (可调整)
    const double memory_weight = 0.4;
    const double latency_weight = 0.3;
    const double load_weight = 0.2;
    const double priority_weight = 0.1;
    
    // 内存得分 (可用内存比例)
    double memory_score = 0.0;
    if (node.total_memory > 0) {
        memory_score = static_cast<double>(node.available_memory - required_memory) 
                       / node.total_memory;
    }
    
    // 延迟得分 (越低越好)
    double latency_score = 1.0 / (1.0 + node.network_latency);
    
    // 负载得分 (1 - 平均负载)
    double load_score = 1.0 - ((node.cpu_usage + node.gpu_utilization) / 200.0);
    
    // 优先级得分 (0-100映射到0.0-1.0)
    double priority_score = node.priority / 100.0;
    
    // 综合得分
    return (memory_weight * memory_score) +
           (latency_weight * latency_score) +
           (load_weight * load_score) +
           (priority_weight * priority_score);
}

Dispatcher::Dispatcher() : running_(false) {}

Dispatcher::~Dispatcher() {
    StopHealthCheck();
}

void Dispatcher::AddNode(RemoteNode&& node) {
    std::lock_guard<std::mutex> lock(mutex);
    nodes.push_back(std::move(node));
}

std::vector<RemoteNode>& Dispatcher::GetNodes() {
    std::lock_guard<std::mutex> lock(mutex);
    return nodes;
}

bool Dispatcher::LoadConfig(const std::string& config_path) {
    std::lock_guard<std::mutex> lock(mutex);
    try {
        YAML::Node config = YAML::LoadFile(config_path);
        
        for (const auto& node : config["nodes"]) {
            RemoteNode remote_node(
                node["id"].as<std::string>(),
                node["name"].as<std::string>(""),
                node["address"].as<std::string>(),
                node["roce_interface"].as<std::string>(""),
                node["priority"].as<int>(50),
                node["total_memory"].as<size_t>(0),
                node["memory"].as<size_t>(0),
                0.0,  // network_latency
                0.0,  // cpu_usage
                0.0   // gpu_utilization
            );
            
    // 初始化Launcher客户端
    remote_node.launcher_client = std::make_unique<LauncherClient>(remote_node.address);
    if (!remote_node.launcher_client->Connect()) {
        std::cerr << "Failed to connect to node: " << remote_node.address << std::endl;
    }
            nodes.push_back(std::move(remote_node));
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return false;
    }
}

RemoteNode* Dispatcher::PickNode(size_t required_memory) {
    std::lock_guard<std::mutex> lock(mutex);
    if (nodes.empty()) return nullptr;

    RemoteNode* best_node = nullptr;
    double best_score = -1.0;

    for (auto& node : nodes) {
        // 跳过内存不足的节点
        if (node.available_memory < required_memory) continue;
        
        double score = CalculateNodeScore(node, required_memory);
        if (score > best_score) {
            best_score = score;
            best_node = &node;
        }
    }

    return best_node;
}

RemoteNode* Dispatcher::GetNodeById(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

RemoteNode* Dispatcher::GetDefaultNode() {
    return nodes.empty() ? nullptr : &nodes[0];
}

void Dispatcher::AddMapping(uint64_t fake_ptr, const RemoteAllocInfo& info) {
    std::lock_guard<std::mutex> lock(mutex);
    memory_map_[fake_ptr] = info;
}

RemoteAllocInfo* Dispatcher::GetMapping(uint64_t fake_ptr) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = memory_map_.find(fake_ptr);
    if (it != memory_map_.end()) {
        return &it->second;
    }
    return nullptr;
}

void Dispatcher::RemoveMapping(uint64_t fake_ptr) {
    std::lock_guard<std::mutex> lock(mutex);
    memory_map_.erase(fake_ptr);
}

// 健康检查线程函数
void HealthCheckThread(Dispatcher* dispatcher) {
    while (dispatcher->IsRunning()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        auto& nodes = dispatcher->GetNodes();
        for (auto& node : nodes) {
        if (node.launcher_client) {
            try {
                // 使用新的LauncherClient接口获取状态
                auto promise = node.launcher_client->GetGpuStatus(node.id);
                auto status = promise.wait(*node.launcher_client->GetWaitScope());
                
                node.available_memory = status.getAvailableMemory();
                node.gpu_utilization = status.getUtilization();
            } catch (const std::exception& e) {
                std::cerr << "Health check failed for node " << node.id 
                          << ": " << e.what() << std::endl;
            }
        }
        }
    }
}

void Dispatcher::StartHealthCheck() {
    if (running_) return;
    
    running_ = true;
    health_check_thread_ = std::thread(HealthCheckThread, this);
}

void Dispatcher::StopHealthCheck() {
    if (!running_) return;
    
    running_ = false;
    if (health_check_thread_.joinable()) {
        health_check_thread_.join();
    }
}
