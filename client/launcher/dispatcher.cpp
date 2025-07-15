#include "dispatcher.h"
#include "launcher_client.h"
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <kj/async.h>
#include <capnp/rpc.h>
#include <algorithm>

// 计算节点综合得分（增强NUMA感知）
double CalculateNodeScore(const RemoteNode& node, size_t required_memory, int numa_node) {
    // 基础权重
    const double memory_weight = 0.3;
    const double latency_weight = 0.2;
    const double load_weight = 0.2;
    const double priority_weight = 0.1;
    const double numa_weight = 0.2; // NUMA亲和性权重
    
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
    
    // NUMA亲和性得分 (相同NUMA节点得1分，跨NUMA得0.5分)
    double numa_score = (node.numa_node == numa_node) ? 1.0 : 0.5;
    
    // 综合得分
    return (memory_weight * memory_score) +
           (latency_weight * latency_score) +
           (load_weight * load_score) +
           (priority_weight * priority_score) +
           (numa_weight * numa_score);
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
                0.0,  // gpu_utilization
                node["numa_node"].as<int>(-1)  // NUMA节点ID
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

// 分配决策核心方法
AllocationPlan Dispatcher::makeAllocationDecision(size_t size, int current_numa) {
    AllocationPlan plan;
    
    // 1. 选择目标节点
    RemoteNode* best_node = nullptr;
    double best_score = -1.0;
    
    for (auto& node : nodes) {
        // 跳过内存不足的节点
        if (node.available_memory < size) continue;
        
        double score = CalculateNodeScore(node, size, current_numa);
        if (score > best_score) {
            best_score = score;
            best_node = &node;
        }
    }
    
    if (!best_node) {
        plan.error = CUDA_ERROR_OUT_OF_MEMORY;
        return plan;
    }
    
    plan.targetNodeId = best_node->id;
    
    // 2. 决定内存类型（显存或主机内存）
    // 如果当前NUMA节点有足够显存，优先使用显存
    if (best_node->numa_node == current_numa && best_node->available_memory > size * 2) {
        plan.memoryType = MemoryType::VRAM;
    } else {
        plan.memoryType = MemoryType::HOST;
    }
    
    // 3. 选择传输协议
    // 如果是GPU到GPU通信且支持RDMA，优先使用RDMA
    if (best_node->rdma_support && size > 1024 * 1024) { // 1MB以上
        plan.transportType = TransportType::RDMA;
    } else if (size > 4096) { // 4KB以上使用UDP
        plan.transportType = TransportType::UDP;
    } else { // 小数据使用TCP
        plan.transportType = TransportType::TCP;
    }
    
    // 4. 预取建议（大块内存或频繁访问）
    plan.prefetchHint = (size > 1024 * 1024); // 大于1MB建议预取
    
    return plan;
}

RemoteNode* Dispatcher::PickNode(size_t required_memory) {
    std::lock_guard<std::mutex> lock(mutex);
    if (nodes.empty()) return nullptr;

    // 使用新的决策方法
    auto plan = makeAllocationDecision(required_memory, -1);
    if (plan.error != CUDA_SUCCESS) return nullptr;
    
    return GetNodeById(plan.targetNodeId);
}

RemoteNode* Dispatcher::GetNodeById(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& node : nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

// 其余代码保持不变...
