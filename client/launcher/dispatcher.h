#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <unordered_map>
#include "launcher_client.h"  // 替换为新的客户端实现

// 远程节点信息
struct RemoteNode {
    RemoteNode() = default;
    
    RemoteNode(std::string id, std::string name, std::string address, 
               std::string roce_interface, int priority,
               size_t total_memory, size_t available_memory,
               double network_latency, double cpu_usage, 
    double gpu_utilization, int numaId)
        : id(std::move(id)), name(std::move(name)), address(std::move(address)),
          roce_interface(std::move(roce_interface)), priority(priority),
          total_memory(total_memory), available_memory(available_memory),
          network_latency(network_latency), cpu_usage(cpu_usage),
          gpu_utilization(gpu_utilization), numaId(numaId) {}
    
    std::string id;
    std::string name;
    std::string address;
    std::string roce_interface;
    int priority;
    size_t total_memory;
    size_t available_memory;
    double network_latency;
    double cpu_usage;
    double gpu_utilization;
    int numaId;  // NUMA节点标识符
    uint16_t zmq_port;  // ZMQ数据传输端口
    std::unique_ptr<LauncherClient> launcher_client;  // 更新为新的客户端类型
};

// 内存映射信息
struct RemoteAllocInfo {
    std::string node_id;
    size_t size;
    uint64_t remote_handle;
};

// 负载均衡调度器
class Dispatcher {
    std::vector<RemoteNode> nodes;
    std::mutex mutex;
    
    // 内存映射表 (fake_ptr → allocation info)
    std::unordered_map<uint64_t, RemoteAllocInfo> memory_map_;
    
    // 健康检查线程
    std::thread health_check_thread_;
    bool running_ = false;

public:
    Dispatcher();
    ~Dispatcher();
    
    // 节点管理
    void AddNode(RemoteNode&& node);
    std::vector<RemoteNode>& GetNodes();
    bool LoadConfig(const std::string& config_path);
    RemoteNode* PickNode(size_t required_memory);  // 保持接口不变
    RemoteNode* GetNodeById(const std::string& id);
    RemoteNode* GetDefaultNode();
    
    // 内存映射管理
    void AddMapping(uint64_t fake_ptr, const RemoteAllocInfo& info);
    RemoteAllocInfo* GetMapping(uint64_t fake_ptr);
    void RemoveMapping(uint64_t fake_ptr);
    
    // 健康检查
    void StartHealthCheck();
    void StopHealthCheck();
};
