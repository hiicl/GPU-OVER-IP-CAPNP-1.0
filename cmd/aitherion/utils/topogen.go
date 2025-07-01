// utils 提供Aitherion工具函数
package utils

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// GenerateTopologyFiles 生成NUMA节点拓扑文件
// 包括GPU、网络接口和内存信息
func GenerateTopologyFiles() error {
	topologyDir := "/var/lib/aitherion/topology"
	if err := os.MkdirAll(topologyDir, 0755); err != nil {
		return fmt.Errorf("创建目录失败: %v", err)
	}

	numaMap := make(map[int]struct {
		GPUs   []string
		IFaces []string
		Memory uint64
	})

	// 扫描GPU设备
	drmPath := "/sys/class/drm"
	drmEntries, _ := ioutil.ReadDir(drmPath)
	for _, entry := range drmEntries {
		if !strings.HasPrefix(entry.Name(), "card") {
			continue
		}
		numaNodePath := filepath.Join(drmPath, entry.Name(), "device/numa_node")
		data, err := os.ReadFile(numaNodePath)
		if err != nil {
			continue
		}
		nodeID, _ := strconv.Atoi(strings.TrimSpace(string(data)))
		if nodeID < 0 {
			continue
		}
		info := numaMap[nodeID]
		info.GPUs = append(info.GPUs, entry.Name())
		numaMap[nodeID] = info
	}

	// 扫描网络接口
	netPath := "/sys/class/net"
	netEntries, _ := ioutil.ReadDir(netPath)
	for _, entry := range netEntries {
		numaNodePath := filepath.Join(netPath, entry.Name(), "device/numa_node")
		data, err := os.ReadFile(numaNodePath)
		if err != nil {
			continue
		}
		nodeID, _ := strconv.Atoi(strings.TrimSpace(string(data)))
		if nodeID < 0 {
			continue
		}
		info := numaMap[nodeID]
		info.IFaces = append(info.IFaces, entry.Name())
		numaMap[nodeID] = info
	}

	// 扫描内存信息
	for node := range numaMap {
		memPath := fmt.Sprintf("/sys/devices/system/node/node%d/meminfo", node)
		data, err := os.ReadFile(memPath)
		if err != nil {
			continue
		}
		for _, line := range strings.Split(string(data), "\n") {
			if strings.HasPrefix(line, "MemTotal:") {
				parts := strings.Fields(line)
				if len(parts) >= 2 {
					memKb, _ := strconv.ParseUint(parts[1], 10, 64)
					info := numaMap[node]
					info.Memory = memKb * 1024
					numaMap[node] = info
				}
				break
			}
		}
	}

	// 输出拓扑文件
	for node, info := range numaMap {
		gpus := strings.Join(info.GPUs, ",")
		ifaces := strings.Join(info.IFaces, ",")
		memGb := float64(info.Memory) / 1e9

		_ = os.WriteFile(filepath.Join(topologyDir, fmt.Sprintf("numa%d_gpus.txt", node)),
			[]byte(gpus), 0644)
		_ = os.WriteFile(filepath.Join(topologyDir, fmt.Sprintf("numa%d_iface.txt", node)),
			[]byte(ifaces), 0644)
		_ = os.WriteFile(filepath.Join(topologyDir, fmt.Sprintf("numa%d_mem_gb.txt", node)),
			[]byte(fmt.Sprintf("%.2f", memGb)), 0644)
	}

	return nil
}
