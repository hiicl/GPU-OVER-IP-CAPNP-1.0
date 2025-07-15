# GPU-over-IP-AC922 项目结构

## 架构定义

- **节点 (Node)**: 指一台物理服务器
- **NUMA**: 每个节点包含两个NUMA域（NUMA0和NUMA1）
- **节点内通信**: 同一服务器内NUMA域间通信（通过X-Bus）
- **跨节点NUMA-to-NUMA通信**: 服务器间通过ZMQ+UDP网络通信
- **跨节点GPU-to-GPU通信**: 服务器间通过RDMA网络通信

## 目录树
```
.
├── client
│   ├── hook
│   │   ├── context_manager.cpp   # CUDA上下文管理
│   │   ├── context_manager.h
│   │   ├── easyhook_entry.cpp    # EasyHook入口点
│   │   ├── hook_cuda.cpp         # CUDA API拦截实现（新增NUMA本地内存优化）
│   │   ├── hook_cuda.def
│   │   ├── hook_cuda.h
│   │   ├── launcher_client.cpp   # Launcher客户端通信
│   │   ├── launcher_client.h
│   │   ├── pch.h                 # 预编译头文件
│   │   ├── rdma_manager.cpp      # RDMA通信管理 (新增ROCE支持)
│   │   ├── rdma_manager.h
│   │   ├── zmq_manager.cpp       # ZMQ通信管理（新增CRC校验支持）
│   │   └── zmq_manager.h
│   └── launcher
│       ├── dispatcher.cpp        # 请求分发器
│       ├── dispatcher.h
│       ├── main.cpp              # Launcher主入口
│       └── protocol_adapter.cpp  # Cap'n Proto协议适配器 (新增RDMA支持)
├── cmd
│   ├── aitherion-cli
│   │   ├── clean.go              # 资源清理工具
│   │   ├── config
│   │   │   └── types.go          # 配置类型定义 (新增RDMA设备配置)
│   │   ├── init.go               # 初始化命令
│   │   ├── main.go               # CLI主入口
│   │   ├── numa
│   │   │   ├── configure.go      # NUMA节点配置 (新增ROCE网卡管理)
│   │   │   ├── discover.go       # NUMA拓扑发现
│   │   │   ├── healthcmd.go      # NUMA健康检查
│   │   │   └── start.go          # 容器启动管理
│   │   └── utils
│   │       ├── docker.go         # Docker工具 (新增RDMA设备映射)
│   │       ├── network.go        # 网络工具
│   │       ├── resource.go       # 资源工具
│   │       └── topogen.go        # 拓扑生成工具
│   └── capnpserver
│       └── main.go               # Cap'n Proto服务器
├── config
│   └── ac922_topology.yaml       # AC922拓扑配置
├── docker
│   └── dockerfile.dockerfile     # Docker构建文件
├── proto
│   ├── common.capnp              # 通用协议定义
│   ├── cuda.capnp                # CUDA相关协议
│   ├── gpu-control.capnp         # GPU控制协议
│   ├── hook-launcher.capnp       # Hook-Launcher通信协议
│   ├── kernel.capnp              # 内核启动协议
│   ├── memcopy.capnp             # 内存复制协议
│   └── proto                     # 协议生成代码
│       ├── common.capnp.c++
│       ├── common.capnp.h
│       ├── cuda.capnp.c++
│       ├── cuda.capnp.h
│       ├── gpu-control.capnp.c++
│       ├── gpu-control.capnp.h
│       ├── hook-launcher.capnp.c++
│       ├── hook-launcher.capnp.h
│       ├── kernel.capnp.c++
│       ├── kernel.capnp.h
│       ├── memcopy.capnp.c++
│       └── memcopy.capnp.h
└── go.mod                        # Go模块定义
```

## 核心文件功能说明

| 文件路径 | 功能描述 |
|----------|----------|
| `client/hook/hook_cuda.cpp` | CUDA API拦截实现，重定向GPU操作到远程节点（新增动态路径决策） |
| `client/hook/rdma_manager.cpp` | RDMA通信管理，实现高性能内存传输 (新增ROCE支持) |
| `client/hook/zmq_manager.cpp` | ZeroMQ通信管理，处理控制平面消息（新增CRC校验支持） |
| `client/launcher/dispatcher.cpp` | 请求分发器（实现中心化策略引擎） |
| `client/launcher/protocol_adapter.cpp` | Cap'n Proto协议适配器，转换消息格式 (新增RDMA内存复制函数) |
| `cmd/aitherion-cli/numa/configure.go` | NUMA节点配置 (新增ROCE网卡管理菜单) |
| `cmd/aitherion-cli/numa/discover.go` | NUMA拓扑发现（新增GPU-NUMA-网卡关系检测） |
| `cmd/aitherion-cli/numa/start.go` | 容器生命周期管理，启动NUMA节点上的服务 |
| `cmd/aitherion-cli/config/types.go` | 配置类型定义（新增动态路径策略类型） |
| `cmd/aitherion-cli/utils/docker.go` | Docker工具 (新增RDMA设备映射支持，新增macvlan自动配置) |
| `proto/hook-launcher.capnp` | Hook与Launcher间通信协议定义（新增动态路径决策接口） |
| `proto/memcopy.capnp` | 内存复制操作协议定义 |
| `docker/dockerfile.dockerfile` | 容器镜像构建配置 |

## CLI使用手册

### ROCE网卡管理
```bash
aitherion-cli numa configure
```

1. 选择`ROCE网卡管理`选项
2. 功能菜单：
   - `添加ROCE网口`：检测并添加支持ROCE/IB的网卡
   - `配置macvlan`：为RDMA设备配置macvlan网络
   - `绑定容器`：将RDMA设备绑定到指定容器
   - `测试RDMA性能`：运行RDMA性能基准测试

### NUMA节点配置示例
```
$ aitherion-cli numa configure

=== NUMA配置菜单 ===
[1] 配置NUMA0
[2] 配置NUMA1
[3] 执行start命令启动容器
[4] ROCE网卡管理
[5] 返回主菜单
请选择操作: 1

=== 配置NUMA0 ===
当前设置:
  内存百分比: 80.0%
  显存占比: 0.8
  控制面接口: eth0
  数据面接口: eth1
[1] 修改内存百分比
[2] 修改显存占比
[3] 修改网络接口
[4] 保存并返回
请选择操作: 3
请输入控制面接口名称: enp1s0
请输入数据面接口名称: enp2s0
请选择操作: 4
```

### ROCE网卡配置示例
```
$ aitherion-cli numa configure

=== NUMA配置菜单 ===
[1] 配置NUMA0
[2] 配置NUMA1
[3] 执行start命令启动容器
[4] ROCE网卡管理
[5] 返回主菜单
请选择操作: 4

=== ROCE网卡管理 ===
[1] 添加ROCE网口
[2] 配置macvlan（自动创建macvlan接口和网络命名空间）
[3] 绑定容器
[4] 测试RDMA性能
[5] 返回上级菜单
请选择操作: 1

正在检测支持ROCE/IB的网卡...
检测到以下支持ROCE/IB的网卡：
[1] 0000:17:00.0 Network controller: Mellanox Technologies MT28908 Family
[2] 0000:d8:00.0 Ethernet controller: Mellanox Technologies MT28908 Family

请选择要添加的网卡 (1-2): 1
已添加网口: 0000:17:00.0
选择绑定的NUMA节点 (可多选, 用逗号分隔):
0: NUMA0
1: NUMA1
> 0,1
绑定到NUMA节点: [0,1]
已保存配置: {PCIAddress:0000:17:00.0 Description:Mellanox... NUMANodes:[0 1]}
```

### 容器启动示例
```
$ aitherion-cli numa start
[Run] docker run -it --rm -d \
    --runtime=nvidia \
    -e NVIDIA_VISIBLE_DEVICES=0 \
    -e CAPNP_PORT=50051 \
    -e ZMQ_PORT=5555 \
    -e CONTROL_IFACE=enp1s0 \
    -e DATA_IFACE=enp2s0 \
    -v /dev:/dev \
    -p 50051:50051 \
    -p 5555:5555/udp \
    --device /dev/infiniband/mlx5_0 \
    --network container:macvlan0 \
    --name aitherion-cli-numa0 \
    -e ENABLE_RDMA=true \
    -e RDMA_DEVICES=mlx5_0 \
    aitherion-runtime:latest ./capnpserver \
    --port 50051 \
    --zmq-port 5555 \
    --control-iface enp1s0 \
    --data-iface enp2s0

[✓] 所有 NUMA 容器启动完成 (使用 Cap'n Proto 协议)
```

## 功能特性

- **GPU虚拟化**：通过客户端Hook机制拦截CUDA调用，实现远程GPU调用
- **内存扩展**：支持使用服务端内存作为显存扩展
- **NUMA感知**：自动优化NUMA节点资源分配（新增本地内存优化）
- **容器化部署**：一键启动服务端NUMA容器集群（新增macvlan自动配置）
- **RDMA支持**：通过RoCE网卡实现GPUDirect RDMA加速
- **可靠传输**：ZeroMQ UDP传输增加CRC校验（新增）
- **双网络平面**：
  - 控制面：1G管理口 + Cap'n Proto TCP
  - 数据面：200G QSFP + ZMQ UDP（带CRC校验）和 RoCE RDMA
- **动态路径决策**：基于NUMA拓扑和GPU位置智能选择传输路径
- **中心化策略服务**：由统一管理内存分配和传输策略

## 性能优化
- **NUMA本地内存优化**：在服务端本地NUMA节点分配内存，减少跨节点访问延迟
- **内存迁移机制**：自动迁移不在本地NUMA节点的内存数据
- **UDP传输优化**：添加CRC校验减少重传，提高有效带宽
- **RoCE网卡加速**：使用RDMA进行GPU数据传输
- **NUMA节点绑定**：优化跨节点访问性能
- **动态路径选择**：根据拓扑和负载自动选择最优路径（NVLink/XBus/RDMA）
- **中心化策略引擎**：综合考虑内存、延迟、负载和优先级
