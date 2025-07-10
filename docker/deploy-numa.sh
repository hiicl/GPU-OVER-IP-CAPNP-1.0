#!/bin/bash

# 参数解析
if [ $# -lt 3 ]; then
    echo "用法: $0 <numa-node> <mem-percent> <vmem-ratio>"
    exit 1
fi

NUMA_NODE=$1
MEM_PERCENT=$2
VMEM_RATIO=$3

# 验证参数
if [[ $NUMA_NODE -ne 0 && $NUMA_NODE -ne 1 ]]; then
    echo "错误：NUMA节点必须是0或1"
    exit 1
fi

if (( $(echo "$MEM_PERCENT > 90" | bc -l) )); then
    echo "错误：内存百分比不能超过90%"
    exit 1
fi

if (( $(echo "$VMEM_RATIO > 0.9" | bc -l) )); then
    echo "错误：显存占比不能超过0.9"
    exit 1
fi

# 加载拓扑配置
TOPOLOGY_FILE="config/ac922_topology.yaml"
if [ ! -f "$TOPOLOGY_FILE" ]; then
    echo "错误：找不到拓扑配置文件 $TOPOLOGY_FILE"
    exit 1
fi

# 解析YAML函数
parse_yaml() {
    local prefix=$2
    local s='[[:space:]]*' w='[a-zA-Z0-9_]*' fs=$(echo @|tr @ '\034')
    sed -ne "s|^\($s\)\($w\)$s:$s\"\(.*\)\"$s\$|\1$fs\2$fs\3|p" \
        -e "s|^\($s\)\($w\)$s:$s\(.*\)$s\$|\1$fs\2$fs\3|p" $1 |
    awk -F$fs '{
        indent = length($1)/2;
        vname[indent] = $2;
        for (i in vname) {if (i > indent) {delete vname[i]}}
        if (length($3) > 0) {
            vn=""; for (i=0; i<indent; i++) {vn=(vn)(vname[i])("_")}
            printf("%s%s%s=\"%s\"\n", "'$prefix'", vn, $2, $3);
        }
    }'
}

# 导入配置变量
eval $(parse_yaml $TOPOLOGY_FILE "topology_")

# 获取NUMA节点总内存
TOTAL_MEM=$(numactl -H | grep "node $NUMA_NODE size" | awk '{print $4}')
if [ -z "$TOTAL_MEM" ]; then
    echo "错误：无法获取NUMA$NUMA_NODE的内存大小"
    exit 1
fi

# 计算内存限制（保留1GB安全余量）
MEM_LIMIT=$(echo "($TOTAL_MEM * $MEM_PERCENT / 100) - 1024" | bc)
if (( $(echo "$MEM_LIMIT < 1024" | bc -l) )); then
    echo "错误：计算后的内存限制${MEM_LIMIT}MB过低，请调整百分比"
    exit 1
fi

# 部署指定NUMA节点
deploy_numa_node() {
    local node_id=$1
    local cpus=$2
    local gpus=$3
    
    echo "部署NUMA$node_id节点 (内存限制: ${MEM_LIMIT}MB, 显存占比: ${VMEM_RATIO})"
    
    # 获取NUMA节点的OpenCAPI设备
    ocapi_devices=$(numa-tool list-ocxl-devices --numa=$node_id)
    
    # 构建设备参数
    device_opts=""
    for device in $ocapi_devices; do
        device_opts+=" --device $device"
    done
    
    docker run -d \
        --name numa$node_id-container \
        --cpuset-cpus=$cpus \
        --gpus="device=$gpus" \
        $device_opts \
        --memory="${MEM_LIMIT}m" \
        -e NUMA_NODE=$node_id \
        -e VMEM_RATIO=$VMEM_RATIO \
        -e CONTROL_IFACE=eth0 \
        -p $((5555 + $node_id)):5555 \
        -p $((6000 + $node_id)):6000 \
        --network=host \
        aitherion-server
}

# 根据NUMA节点选择配置
case $NUMA_NODE in
    0)
        deploy_numa_node 0 "$topology_numa_nodes_0_cpus" $(echo ${topology_numa_nodes_0_gpus[@]} | tr ' ' ',')
        ;;
    1)
        deploy_numa_node 1 "$topology_numa_nodes_1_cpus" $(echo ${topology_numa_nodes_1_gpus[@]} | tr ' ' ',')
        ;;
    *)
        echo "错误：不支持的NUMA节点 $NUMA_NODE"
        exit 1
        ;;
esac

# 添加容器健康检查
check_container_health() {
    container_name=$1
    echo "检查容器 $container_name 的健康状态..."
    
    # 检查容器是否运行
    if [ "$(docker inspect -f '{{.State.Running}}' $container_name)" != "true" ]; then
        echo "错误：容器 $container_name 未运行"
        return 1
    fi
    
    # 检查OpenCAPI设备状态
    docker exec $container_name numa-tool check-ocxl-health
    return $?
}

# 等待容器启动
sleep 5

# 执行健康检查
check_container_health "numa${NUMA_NODE}-container"
if [ $? -ne 0 ]; then
    echo "警告：容器健康检查未通过，请检查日志"
fi

echo "NUMA节点${NUMA_NODE}容器部署完成: numa${NUMA_NODE}-container"
