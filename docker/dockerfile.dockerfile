# Dockerfile 定义用于构建和运行GPU管理服务的容器镜像

# Stage 1: 构建Go二进制文件
# 使用官方Go镜像作为构建环境
FROM golang:1.21-bullseye AS builder

# 设置工作目录
WORKDIR /app

# 复制依赖文件并下载依赖
COPY go.mod go.sum ./
RUN go mod download

# 复制所有源代码到容器中
COPY . .

# 构建Go服务二进制文件
# CGO_ENABLED=0: 禁用CGO，构建静态链接的可执行文件
# GOOS=linux: 目标操作系统为Linux
# GOARCH=ppc64le: 目标架构为ppc64le（适用于Power系统）
# -o aitherion-server: 输出文件名为aitherion-server
RUN CGO_ENABLED=0 GOOS=linux GOARCH=ppc64le go build -o aitherion-server ./cmd/grpcserver

# Stage 2: 创建最小化运行时环境
# 使用Debian精简版作为基础镜像
FROM debian:bullseye-slim

# 设置工作目录
WORKDIR /aitherion

# 从构建阶段复制构建好的Go服务二进制文件
COPY --from=builder /app/aitherion-server .

# 安装必要的运行时依赖
# --no-install-recommends: 不安装推荐包，减小镜像体积
# 安装包包括: 
#   ca-certificates - HTTPS证书
#   curl - 网络工具
#   bash - shell环境
#   coreutils - 核心工具集
# 清理apt缓存以减小镜像大小
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl bash coreutils && \
    rm -rf /var/lib/apt/lists/*

# 注意：NVIDIA驱动和CUDA库由宿主机提供，不在容器内安装

# 设置容器启动时执行的命令
ENTRYPOINT ["./aitherion-server"]
