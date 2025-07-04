#include <iostream>
#include <thread>
#include <memory>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <capnp/ez-rpc.h>
#include "dispatcher.h"
#include "hook-launcher.capnp.h" // 更新为新的协议文件

namespace fs = std::filesystem;

// HookLauncher接口实现
class HookLauncherImpl final : public HookLauncher::Server {
public:
    HookLauncherImpl(Dispatcher& dispatcher) : dispatcher_(dispatcher) {}

    // 内存分配请求
    kj::Promise<void> requestAllocation(RequestAllocationContext context) override {
        auto size = context.getParams().getSize();
        
        // 选择最佳节点
        auto* node = dispatcher_.PickNode(size);
        if (!node) {
            context.getResults().setResult(AllocationResult{
                .fakePtr = 0,
                .error = CUDA_ERROR_OUT_OF_MEMORY
            });
            return kj::READY_NOW;
        }
        
        // 在远程节点分配内存
        auto allocPromise = node->capnp_client->MemAlloc(size);
        return allocPromise.then([this, context, node](auto response) mutable {
            if (response.error != CUDA_SUCCESS) {
                context.getResults().setResult(AllocationResult{
                    .fakePtr = 0,
                    .error = response.error
                });
                return;
            }
            
            // 创建内存映射
            uint64_t fakePtr = reinterpret_cast<uint64_t>(::operator new(1));
            RemoteAllocInfo allocInfo{
                .node_id = node->id,
                .size = size,
                .remote_handle = response.ptr
            };
            dispatcher_.AddMapping(fakePtr, allocInfo);
            
            // 返回分配结果
            context.getResults().setResult(AllocationResult{
                .fakePtr = fakePtr,
                .error = CUDA_SUCCESS
            });
        });
    }

    // 内存释放请求
    kj::Promise<void> requestFree(RequestFreeContext context) override {
        auto fakePtr = context.getParams().getFakePtr();
        
        // 获取内存映射信息
        auto* allocInfo = dispatcher_.GetMapping(fakePtr);
        if (!allocInfo) {
            context.getResults().setResult(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        // 获取对应节点
        auto* node = dispatcher_.GetNodeById(allocInfo->node_id);
        if (!node) {
            context.getResults().setResult(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        // 在远程节点释放内存
        auto freePromise = node->capnp_client->MemFree(allocInfo->remote_handle);
        return freePromise.then([this, context, fakePtr](auto error) mutable {
            // 移除内存映射
            dispatcher_.RemoveMapping(fakePtr);
            ::operator delete(reinterpret_cast<void*>(fakePtr), 1);
            context.getResults().setResult(error);
        });
    }

    // HtoD传输计划请求
    kj::Promise<void> planMemcpyHtoD(PlanMemcpyHtoDContext context) override {
        auto dstFakePtr = context.getParams().getDstFakePtr();
        auto size = context.getParams().getSize();
        
        // 获取内存映射信息
        auto* allocInfo = dispatcher_.GetMapping(dstFakePtr);
        if (!allocInfo) {
            context.getResults().initPlan().setError(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        // 获取对应节点
        auto* node = dispatcher_.GetNodeById(allocInfo->node_id);
        if (!node) {
            context.getResults().initPlan().setError(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        // 生成传输计划
        auto plan = context.getResults().initPlan();
        plan.setTargetServerIp(node->address);
        plan.setTargetServerZmqPort(node->zmq_port); // 使用节点配置的ZMQ端口
        plan.setRemotePtr(allocInfo->remote_handle);
        plan.setError(CUDA_SUCCESS);
        
        return kj::READY_NOW;
    }

private:
    Dispatcher& dispatcher_;
};

// 配置文件监视器
void ConfigWatcher(Dispatcher& dispatcher, const std::string& configPath) {
    auto lastWriteTime = fs::last_write_time(configPath);
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        try {
            auto currentWriteTime = fs::last_write_time(configPath);
            if (currentWriteTime != lastWriteTime) {
                std::cout << "检测到配置文件变化，重新加载配置..." << std::endl;
                dispatcher.LoadConfig(configPath);
                lastWriteTime = currentWriteTime;
            }
        } catch (...) {
            std::cerr << "配置文件监视错误" << std::endl;
        }
    }
}

int main() {
    const std::string configPath = "config/scheduler_policy.yaml";
    
    // 初始化全局调度器
    Dispatcher dispatcher;
    dispatcher.LoadConfig(configPath);

    // 创建RPC服务
    capnp::EzRpcServer server(kj::heap<HookLauncherImpl>(dispatcher), "127.0.0.1:12345"); // 使用标准端口
    
    // 启动服务
    auto& waitScope = server.getWaitScope();
    uint16_t port = server.getPort().wait(waitScope);
    std::cout << "Launcher RPC服务已启动，端口: " << port << std::endl;

    // 运行健康检查线程
    std::thread healthCheckThread([&]{
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // 增强健康检查：包括内存和GPU利用率监控
            dispatcher.PerformHealthCheck();
            
            // 记录节点状态
            auto nodes = dispatcher.GetNodes();
            for (const auto& node : nodes) {
                std::cout << "节点 " << node.id << " - "
                          << "可用内存: " << node.available_memory << " MB, "
                          << "GPU利用率: " << node.gpu_utilization << "%" 
                          << std::endl;
            }
        }
    });

    // 运行配置监视线程
    std::thread configWatcherThread([&]{
        ConfigWatcher(dispatcher, configPath);
    });

    // 设置线程分离
    healthCheckThread.detach();
    configWatcherThread.detach();

    // 等待服务终止
    kj::NEVER_DONE.wait(waitScope);
    return 0;
}
