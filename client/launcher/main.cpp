#include <iostream>
#include <thread>
#include <memory>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <capnp/ez-rpc.h>
#include "dispatcher.h"
#include "hook-launcher.capnp.h"
#include "services/memory_service.h"
#include "services/transport_service.h"
#include "services/advise_service.h"
#include "services/cooling_service.h" // 用于AdviseService
#include "transport_manager.h" // 用于TransportService

namespace fs = std::filesystem;

// 根服务聚合所有接口
class RootService final : 
    public HookLauncher::Server,
    public GenericServices::Server {
public:
    RootService(
        Dispatcher& dispatcher,
        GlobalMemoryManager& memoryManager,
        TransportManager& transportManager,
        CoolingService& coolingService
    ) : dispatcher_(dispatcher),
        memoryService_(memoryManager),
        transportService_(transportManager),
        adviseService_(coolingService) {}

    // HookLauncher接口实现
    kj::Promise<void> requestAllocation(RequestAllocationContext context) override {
        auto size = context.getParams().getSize();
        
        auto* node = dispatcher_.PickNode(0, size); // 使用0作为ptr占位符
        if (!node) {
            context.getResults().setResult(AllocationResult{
                .fakePtr = 0,
                .error = CUDA_ERROR_OUT_OF_MEMORY
            });
            return kj::READY_NOW;
        }
        
        auto allocPromise = node->launcher_client->requestAllocation(size);
        return allocPromise.then([this, context, node](auto response) mutable {
            if (response.error != CUDA_SUCCESS) {
                context.getResults().setResult(AllocationResult{
                    .fakePtr = 0,
                    .error = response.error
                });
                return;
            }
            
            uint64_t fakePtr = reinterpret_cast<uint64_t>(::operator new(1));
            RemoteAllocInfo allocInfo{
                .node_id = node->id,
                .size = response.size,
                .remote_handle = response.handle
            };
            dispatcher_.AddMapping(fakePtr, allocInfo);
            
            context.getResults().setResult(AllocationResult{
                .fakePtr = fakePtr,
                .error = CUDA_SUCCESS
            });
        });
    }

    kj::Promise<void> requestFree(RequestFreeContext context) override {
        auto fakePtr = context.getParams().getFakePtr();
        
        auto* allocInfo = dispatcher_.GetMapping(fakePtr);
        if (!allocInfo) {
            context.getResults().setResult(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        auto* node = dispatcher_.GetNodeById(allocInfo->node_id);
        if (!node) {
            context.getResults().setResult(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        auto freePromise = node->launcher_client->requestFree(allocInfo->remote_handle);
        return freePromise.then([this, context, fakePtr](auto error) mutable {
            dispatcher_.RemoveMapping(fakePtr);
            ::operator delete(reinterpret_cast<void*>(fakePtr), 1);
            context.getResults().setResult(error);
        });
    }

    kj::Promise<void> planMemcpyHtoD(PlanMemcpyHtoDContext context) override {
        // 保持原有实现不变
        auto dstFakePtr = context.getParams().getDstFakePtr();
        auto size = context.getParams().getSize();
        
        auto* allocInfo = dispatcher_.GetMapping(dstFakePtr);
        if (!allocInfo) {
            context.getResults().initPlan().setError(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        auto* node = dispatcher_.GetNodeById(allocInfo->node_id);
        if (!node) {
            context.getResults().initPlan().setError(CUDA_ERROR_INVALID_VALUE);
            return kj::READY_NOW;
        }
        
        auto plan = context.getResults().initPlan();
        plan.setTargetServerIp(node->address);
        plan.setTargetServerZmqPort(node->zmq_port);
        plan.setRemotePtr(allocInfo->remote_handle);
        plan.setError(CUDA_SUCCESS);
        
        return kj::READY_NOW;
    }

    // GenericServices接口实现
    kj::Promise<void> allocateMemory(AllocateMemoryContext context) override {
        return memoryService_.allocateMemory(context);
    }
    
    kj::Promise<void> freeMemory(FreeMemoryContext context) override {
        return memoryService_.freeMemory(context);
    }
    
    kj::Promise<void> executeTransfer(ExecuteTransferContext context) override {
        return transportService_.executeTransfer(context);
    }
    
    kj::Promise<void> handleMemAdvise(HandleMemAdviseContext context) override {
        return adviseService_.handleMemAdvise(context);
    }

private:
    Dispatcher& dispatcher_;
    MemoryService memoryService_;
    TransportService transportService_;
    AdviseService adviseService_;
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

    // 创建服务依赖
    GlobalMemoryManager memoryManager;
    TransportManager transportManager;
    CoolingService coolingService;
    
    // 创建聚合服务
    capnp::EzRpcServer server(
        kj::heap<RootService>(dispatcher, memoryManager, transportManager, coolingService), 
        "127.0.0.1:12345"
    );
    
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
