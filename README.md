# GPU-over-IP-AC922 项目结构

## 架构定义

### 物理设备
- **Server/Host**: IBM AC922物理服务器或运行Linux的主机
- **Numa节点**: CPU + 本地内存 + 局部GPU接口的拓扑单元
- **Numa地址**: 唯一标识符格式`serverId:numaId`

### 通信路径
| 路径类型 | 技术栈 | 描述 |
|----------|--------|------|
| GPU-to-GPU | ROCE + GPUDirect RDMA | GPU间跨服务器直接通信 |
| Numa-to-Numa | UDP + ZMQ | Numa域间跨服务器通信 |
| Server内Numa通信 | X-Bus | 同一服务器内Numa域间通信 |

### 调度与数据管理
- **调度单位**: Numa节点（所有资源调度基于Numa维度）
- **热度管理**: 以Numa节点为数据所属单位统计访问热度
- **位置标识**: 使用`serverId:numaId`格式唯一标识资源位置

## 目录树
```
.
├── client
│   ├── data_transfer             # 数据传输库
│   │   ├── include
│   │   │   └── data_transfer.h   # 数据传输头文件
│   │   └── src
│   │       ├── data_transfer.cpp # 数据传输实现
│   │       └── zmq_manager.cpp   # ZMQ管理器
│   ├── hook
│   │   ├── easyhook_entry.cpp    # EasyHook入口点
│   │   ├── hook_cuda.cpp         # CUDA API拦截实现
│   │   ├── hook_cuda.def
│   │   ├── hook_cuda.h
│   │   ├── launcher_client.cpp   # Launcher客户端通信
│   │   ├── launcher_client.h
│   │   └── pch.h                 # 预编译头文件（解决C2894关键组件）
│   └── launcher
│       ├── dispatcher.cpp        # 请求分发器
│       ├── dispatcher.h
│       ├── launcher_client.cpp   # Launcher服务端客户端
│       ├── launcher_client.h
│       ├── main.cpp              # Launcher主入口
│       ├── memory
│       │   ├── global_memory.cpp # 全局内存管理
│       │   ├── global_memory.h
│       │   └── numa_address.h    # Numa地址标识
│       ├── protocol_adapter.cpp  # Cap'n Proto协议适配器
│       └── transport
│           ├── plank
│           │   ├── plank_transport.cpp # 跳板传输实现
│           │   └── plank_transport.h
│           ├── rdma_transport.cpp # RDMA传输实现
│           ├── rdma_transport.h
│           ├── zmq_transport.cpp # ZeroMQ传输实现
│           └── zmq_transport.h
│       └── services
│           ├── advise_service.cpp   # 内存建议服务实现
│           ├── advise_service.h
│           ├── cooling_service.cpp  # 冷却服务实现
│           ├── cooling_service.h
│           ├── memory_service.cpp   # 内存服务实现
│           ├── memory_service.h
│           ├── transport_service.cpp # 传输服务实现
│           └── transport_service.h
├── cmd
│   ├── aitherion-cli
│   │   ├── clean.go              # 资源清理工具
│   │   ├── config
│   │   │   └── types.go          # 配置类型定义
│   │   ├── init.go               # 初始化命令
│   │   ├── main.go               # CLI主入口
│   │   ├── numa
│   │   │   ├── configure.go      # Numa节点配置
│   │   │   ├── discover.go       # Numa拓扑发现
│   │   │   ├── healthcmd.go      # Numa健康检查
│   │   │   └── start.go          # 容器启动管理
│   │   └── utils
│   │       ├── docker.go         # Docker工具
│   │       ├── network.go        # 网络工具
│   │       ├── resource.go       # 资源工具
│   │       └── topogen.go        # 拓扑生成工具
│   └── capnpserver
│       └── main.go               # Cap'n Proto服务器
├── docker
│   └── dockerfile.dockerfile     # Docker构建文件
├── proto
│   ├── common.capnp              # 通用协议定义
│   ├── common.capnp.c++          # 协议生成代码
│   ├── common.capnp.h
│   ├── cuda.capnp                # CUDA相关协议
│   ├── cuda.capnp.c++
│   ├── cuda.capnp.h
│   ├── gpu-control.capnp         # GPU控制协议
│   ├── gpu-control.capnp.c++
│   ├── gpu-control.capnp.h
│   ├── hook-launcher.capnp       # Hook-Launcher通信协议
│   ├── hook-launcher.capnp.c++
│   ├── hook-launcher.capnp.h
│   ├── kernel.capnp              # 内核启动协议
│   ├── kernel.capnp.c++
│   ├── kernel.capnp.h
│   ├── memcopy.capnp             # 内存复制协议
│   ├── memcopy.capnp.c++
│   └── memcopy.capnp.h
└── go.mod                        # Go模块定义
```

## 核心文件功能说明

| 文件路径 | 功能描述 |
|----------|----------|
| `client/hook/hook_cuda.cpp` | CUDA API拦截实现（仅保留拦截逻辑，完全解耦服务依赖） |
| `client/data_transfer/src/data_transfer.cpp` | 数据传输核心实现（独立服务） |
| `client/data_transfer/src/zmq_manager.cpp` | ZMQ连接管理（带自动重连） |
| `client/hook/pch.h` | 预编译头文件（解决C++模板作用域冲突） |
| `client/launcher/services/memory_service.cpp` | 内存分配/释放服务 |
| `client/launcher/services/transport_service.cpp` | 数据传输协调服务 |
| `client/launcher/services/advise_service.cpp` | 内存建议服务（预取/位置建议） |
| `client/launcher/services/cooling_service.cpp` | 数据热度监控服务 |
| `client/launcher/dispatcher.cpp` | 请求分发器（动态路径决策+显存稳定区管理） |
| `client/launcher/transport/zmq_transport.cpp` | ZeroMQ UDP传输实现 |
| `client/launcher/transport/rdma_transport.cpp` | RDMA ROCE传输实现 |
| `client/launcher/transport/plank/plank_transport.cpp` | 跳板传输实现（GDR-to-GDR） |
| `cmd/aitherion-cli/numa/configure.go` | Numa节点配置 |
| `proto/hook-launcher.capnp` | Hook与Launcher间通信协议 |

## 架构优化亮点

1. **职责分离**：
   - Hook层仅负责API拦截（`hook_cuda.cpp`）
   - 数据传输由独立服务处理（`data_transfer`）
   - Launcher负责协调和服务管理

2. **编译问题解决方案**：
   - 重构`pch.h`解决C2894模板作用域冲突
   - 严格分离C/C++头文件作用域

3. **三维数据特性增强**：
   ```mermaid
   graph TD
     A[数据特性] --> B(热度)
     A --> C(温度)
     A --> D(流动性)
     A --> E(稳定性)
     B --> F[访问频率统计]
     C --> G[实时热度量化]
     D --> H[跨节点迁移追踪]
     E --> I[生存周期评估]
     F --> J[自动预取]
     G --> K[冷热数据分层]
     H --> L[迁移优化]
     I --> M[显存稳定区管理]
   ```

4. **服务层接口规范**：
   - 所有服务通过`launcher_client.h`明确定义接口
   - 严格的前向声明避免头文件污染

## 关键功能实现验证

### Numa路径管理
1. **同一Numa NVLink**  
   ✅ Linux自动管理 - 项目无需实现  
2. **跨Numa X-Bus路径**  
   ✅ 内存本地分配与自动迁移  
3. **跨节点RDMA调度**  
   ✅ 智能传输策略选择  
   ✅ GDR-to-GDR传输支持  
4. **动态路径决策**  
   ✅ 基于拓扑的智能路由  
   ✅ 读写路径分离机制  

### 智能数据迁移特性
1. **高频数据本地化**  
   ✅ 频繁访问的远程数据自动迁移至GPU所在Numa  
   ✅ 减少跨Numa访问延迟  
2. **流动性感知迁移**  
   ✅ 高流动性数据优先迁移到本地主机内存  
   ✅ 迁移计数阈值触发自动优化  
3. **双重阈值稳定区管理**  
   | 阈值 | 操作 | 效果 |  
   |------|------|------|  
   | >85% 显存利用率 | 触发迁移机制 | 防止显存过载 |  
   | <70% 显存利用率 | 扩展稳定区 | 优化资源利用率 |  

### 服务化架构设计
1. **服务层架构**  
   ✅ 模块化服务设计（内存/传输/建议/冷却服务）  
   ✅ 解耦核心功能便于扩展  
2. **控制面Cap'n Proto**  
   ✅ 所有RPC接口使用Cap'n Proto  
3. **ZeroMQ UDP数据面**  
   ✅ Numa绑定的网卡支持  
   ✅ CRC校验保障可靠性  
4. **RDMA独立设计**  
   ✅ 专用ROCE通道  
   ✅ 跨Numa优化支持  
5. **跳板传输机制**  
   ✅ GPU-CPU零拷贝  
   ✅ GDR-to-GDR传输优化  
6. **三维数据特性服务**  
   ✅ **热度分析**：基于访问频率和时间衰减  
   ✅ **流动性追踪**：记录跨Numa/节点迁移次数  
   ✅ **稳定性评估**：结合地址生存周期和访问模式  
   ✅ **显存稳定区**：自动管理高稳定性热数据  
   ✅ **利用率阈值策略**：  
     - >85%利用率触发迁移  
     - <70%利用率扩展稳定区  
   ✅ **温度指标**：实时监控数据访问热度  

## 使用示例

```bash
# Numa拓扑发现
aitherion-cli numa discover

# 配置Numa节点
aitherion-cli numa configure

# 启动服务
aitherion-cli numa start
```

## 功能特性

- **服务化架构**：
  - 内存服务：统一管理内存分配/释放
  - 传输服务：处理数据传输和内核启动
  - 建议服务：优化内存位置和预取策略
  - 冷却服务：三维数据特性监控
- **GPU虚拟化**：通过客户端Hook机制拦截CUDA调用
- **Numa感知**：自动优化Numa节点资源分配
- **容器化部署**：一键启动服务端容器集群
- **双网络平面**：
  - 控制面：Cap'n Proto TCP
  - 数据面：ZeroMQ UDP + RDMA ROCE
- **动态路径决策**：智能选择最优传输路径（NVLink/XBus/RDMA）
- **三维数据特性管理**：
  - 热度：实时监控访问频率
  - 温度：量化数据访问热度
  - 流动性：追踪跨节点迁移
  - 稳定性：评估数据生存周期
- **读写路径分离**：
  - 读路径：GPUDirect RDMA（支持GDR-to-GDR）
  - 写路径：ZMQ over UDP
- **显存稳定区**：自动管理高稳定性热数据
- **跳板传输机制**：高效处理GDR-to-GDR通信
- **资源聚集**：自动优化内存布局
- **智能数据迁移**：
  - 高频数据本地化
  - 流动性感知迁移
  - 双重阈值稳定区管理

## 性能优化
- **服务层优化**：
  - 内存服务：Numa本地内存分配
  - 传输服务：智能选择最优传输协议
  - 建议服务：自动预取热点数据
- **Numa本地内存优化**：减少跨节点访问延迟
- **UDP传输优化**：CRC校验减少重传
- **RoCE网卡加速**：GPU间直接RDMA传输
- **动态路径选择**：实时优化传输路径
- **三维数据特性优化**：
  - 热数据本地化处理
  - 冷数据跨节点传输
- **Numa预取优化**：
  - 跨Numa访问自动启用预取
  - 本地内存优先分配
- **跳板传输**：优化GDR-to-GDR通信延迟
