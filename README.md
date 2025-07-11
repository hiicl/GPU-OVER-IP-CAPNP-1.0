# GPU-over-IP-AC922 项目结构

## 目录树
```
.
├── client
│   ├── hook
│   │   ├── context_manager.cpp   # CUDA上下文管理
│   │   ├── context_manager.h
│   │   ├── easyhook_entry.cpp    # EasyHook入口点
│   │   ├── hook_cuda.cpp         # CUDA API拦截实现
│   │   ├── hook_cuda.def
│   │   ├── hook_cuda.h
│   │   ├── zmq_manager.cpp       # ZMQ通信管理
│   │   └── zmq_manager.h
│   └── launcher
│       ├── dispatcher.cpp        # 请求分发器
│       ├── dispatcher.h
│       ├── launcher_client.cpp   # Launcher客户端
│       ├── launcher_client.h
│       ├── main.cpp              # Launcher主入口
│       └── protocol_adapter.cpp  # 协议适配器
├── cmd
│   ├── aitherion-cli
│   │   ├── clean.go              # 资源清理工具
│   │   ├── config
│   │   │   └── types.go          # 配置类型定义
│   │   ├── init.go               # 初始化命令
│   │   ├── main.go               # CLI主入口
│   │   ├── numa
│   │   │   ├── configure.go      # NUMA节点配置
│   │   │   ├── discover.go       # NUMA拓扑发现
│   │   │   ├── healthcmd.go      # NUMA健康检查
│   │   │   └── start.go          # 容器启动管理
│   │   └── utils
│   │       ├── docker.go         # Docker工具
│   │       ├── network.go        # 网络工具
│   │       ├── resource.go       # 资源工具
│   │       └── topogen.go        # 拓扑生成工具
│   └── capnpserver
│       └── main.go               # Cap'n Proto服务器
├── pkg
│   ├── net
│   │   ├── zmq_sender.go         # ZeroMQ发送端实现
│   │   └── zmq_receiver.go       # ZeroMQ接收端实现
│   └── numa
│       ├── binding.go            # NUMA绑定工具
│       ├── discovery.go          # NUMA发现实现
│       ├── opencapi.go           # OpenCAPI支持
│       └── routing.go            # 路由管理
├── docker
│   └── dockerfile.dockerfile     # Dockerfile
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
│       └── hook-launcher.capnp.h
└── go.mod                        # Go模块定义
```

## 核心文件功能说明

| 文件路径 | 功能描述 |
|----------|----------|
| `pkg/numa/discovery.go` | NUMA拓扑发现实现，生成设备映射 |
| `pkg/net/zmq_receiver.go` | ZeroMQ接收端实现，处理内存传输 |
| `client/hook/hook_cuda.cpp` | CUDA API拦截层，重定向GPU操作 |
| `cmd/aitherion-cli/numa/start.go` | 容器生命周期管理入口 |
| `cmd/aitherion-cli/numa/discover.go` | NUMA资源检测与拓扑发现 |
| `proto/common.capnp` | 定义通用数据类型和错误代码 |
| `docker/dockerfile.dockerfile` | 容器构建配置 |
