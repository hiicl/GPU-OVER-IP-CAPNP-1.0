#include "dispatcher.h"
#include "launcher_client.h"
#include "services/cooling_service.h" // 添加冷却服务头文件
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <kj/async.h>
#include <capnp/rpc.h>
#include <algorithm>
#include <chrono> // 添加时间支持
#include <iomanip> // 用于std::put_time

// 计算节点综合得分（增强NUMA感知）
double CalculateNodeScore(const RemoteNode& node, size_t required_memory, int numaId) {
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
    double numa_score = (node.numaId == numaId) ? 1.0 : 0.5;
    
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
                node["numaId"].as<int>(-1)  // NUMA节点ID
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

// 增强的分配决策核心方法
AllocationPlan Dispatcher::makeAllocationDecision(uintptr_t ptr, size_t size, int current_numa) {
    AllocationPlan plan;
    
    // 获取冷却服务实例
    auto& cooling = CoolingService::Instance();
    
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
    
    // 2. 获取三维数据特性
    bool is_hot = cooling.isHotData(ptr);
    uint32_t mobility = cooling.getMobility(ptr);
    float stability = cooling.getStability(ptr);
    int numaId = cooling.getNumaId(ptr);  // 使用numaId
    float temperature = cooling.getTemperature(ptr);
    
    // 3. 决定内存位置（基于稳定性和热度）
    if (stability > 0.8f && is_hot) {
        // 高稳定性热数据：驻留显存
        plan.memoryType = MemoryType::VRAM;
    } else if (mobility > 5) {
        // 高流动性数据：主机内存（方便RDMA读+UDP写）
        plan.memoryType = MemoryType::HOST;
    } else {
        // 其他情况：根据NUMA位置决定
        if (best_node->numaId == numaId && best_node->available_memory > size * 2) {
            plan.memoryType = MemoryType::VRAM;
        } else {
            plan.memoryType = MemoryType::HOST;
        }
    }
    
    // 调试日志 - 显示三维数据特性
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::cout << "[" << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S")
              << "] Data at 0x" << std::hex << ptr << std::dec 
              << " - Heat: " << (is_hot ? "HOT" : "COLD")
              << ", Temp: " << temperature
              << ", Mobility: " << mobility
              << ", Stability: " << stability
              << ", NUMA: " << numaId  // 使用numaId
              << " (size: " << size << " bytes)"
              << std::endl;
    
    // 4. 根据数据特性选择传输协议（实现读写分离）
    if (is_hot && stability > 0.8f) {
        // 稳定热数据：本地处理
        plan.transportType = TransportType::LOCAL;
        std::cout << "  - Using local processing for stable hot data" << std::endl;
    } else if (is_hot && mobility < 3) {
        // 低流动性热数据：优先使用RDMA
        if (best_node->rdma_support) {
            plan.transportType = TransportType::RDMA;
            std::cout << "  - Using RDMA for hot data with low mobility" << std::endl;
        } else {
            plan.transportType = TransportType::UDP;
            std::cout << "  - RDMA not available, using UDP for hot data" << std::endl;
        }
    } else {
        // 读写分离：读操作用RDMA，写操作用UDP
        plan.transportType = TransportType::RDMA_UDP;
        std::cout << "  - Using RDMA for reads and UDP for writes" << std::endl;
    }
    
    // 5. NUMA拓扑优化
    if (best_node->numaId != -1 && numaId != -1) {
        if (best_node->numaId == numaId) {
            std::cout << "  - NUMA match: source and target on same NUMA node (" 
                      << numaId << ")" << std::endl;
            plan.numaMatch = true;
        } else {
            std::cout << "  - NUMA mismatch: source=" << numaId 
                      << ", target=" << best_node->numaId << std::endl;
            plan.numaMatch = false;
            
            // 跨NUMA优化：增加预取提示
            plan.prefetchHint = true;
            std::cout << "  - Enabling prefetch for cross-NUMA transfer" << std::endl;
        }
    } else {
        plan.numaMatch = false;
    }
    
    // 6. GPU拓扑优化
    if (best_node->nvlink_support) {
        std::cout << "  - Target node has NVLink support" << std::endl;
        plan.gpuTopologyOptimal = true;
    } else {
        std::cout << "  - Target node does not have NVLink support" << std::endl;
        plan.gpuTopologyOptimal = false;
    }
    
    // 7. 显存利用率阈值策略
    if (best_node->gpu_memory_utilization > 0.85f) {
        // 显存利用率 > 85%：触发迁移机制
        plan.triggerMigration = true;
        std::cout << "  - GPU memory utilization > 85%, triggering migration" << std::endl;
    } else if (best_node->gpu_memory_utilization < 0.7f) {
        // 显存利用率 < 70%：扩展稳定区
        plan.expandStableZone = true;
        std::cout << "  - GPU memory utilization < 70%, expanding stable zone" << std::endl;
    }
    
    // 8. GDR-to-GDR传输支持
    if (best_node->gdr_support && mobility > 0) {
        plan.gdrTransfer = true;
        std::cout << "  - Using GDR-to-GDR transfer for high mobility data" << std::endl;
    }
    
    return plan;
}

RemoteNode* Dispatcher::PickNode(uintptr_t ptr, size_t required_memory) {
    std::lock_guard<std::mutex> lock(mutex);
    if (nodes.empty()) return nullptr;

    // 使用新的决策方法，传递内存指针
    auto plan = makeAllocationDecision(ptr, required_memory, -1);
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
