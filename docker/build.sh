#!/bin/bash
# docker/build.sh - 用于构建Docker镜像的脚本
# 使用说明：在项目根目录下运行此脚本（./docker/build.sh [arch]）
#   可选参数 arch: 目标架构（amd64 或 ppc64le），默认为 amd64

# 设置安全选项：
# -e: 当任何命令返回非零状态时立即退出，确保脚本安全执行
set -e

# 定义镜像名称和标签
IMAGE_NAME=aitherion-server  # Docker镜像名称
TAG=latest              # Docker镜像标签，默认为latest

# 检查架构参数
ARCH=${1:-amd64}
if [[ "$ARCH" != "amd64" && "$ARCH" != "ppc64le" ]]; then
    echo "错误：不支持的架构 '$ARCH'，请使用 amd64 或 ppc64le"
    exit 1
fi

# 执行Docker构建命令，指定目标平台
# --platform linux/$ARCH: 指定目标架构
# -t ${IMAGE_NAME}:${TAG}: 指定镜像名称和标签
# -f docker/Dockerfile: 指定Dockerfile路径
# . : 构建上下文为当前目录（项目根目录）
docker build --platform linux/$ARCH -t ${IMAGE_NAME}:${TAG} -f docker/Dockerfile .
