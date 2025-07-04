package numa

import (
	"bufio"
	"os"
	"strconv"
	"strings"
)

// Node 表示NUMA节点信息
type Node struct {
	ID      int      // 节点ID
	CPUs    []int    // 节点关联的CPU核心
	Memory  uint64   // 节点内存大小（字节）
	Devices []string // 节点关联的设备ID
}

// DiscoverNodes 发现系统中的NUMA节点
// IsSameNode 检查两个设备是否在同一NUMA节点
func IsSameNode(deviceA, deviceB string) bool {
	// 实际实现中从设备ID提取NUMA节点信息
	return deviceA[:3] == deviceB[:3]
}

func DiscoverNodes() ([]Node, error) {
	nodes := []Node{}
	
	// 解析/sys/devices/system/node目录
	files, err := os.ReadDir("/sys/devices/system/node")
	if err != nil {
		return nil, err
	}
	
	for _, f := range files {
		if !f.IsDir() || !strings.HasPrefix(f.Name(), "node") {
			continue
		}
		
		nodeID, _ := strconv.Atoi(f.Name()[4:])
		node := Node{ID: nodeID}
		
		// 读取CPU列表
		if cpuFile, err := os.Open("/sys/devices/system/node/" + f.Name() + "/cpulist"); err == nil {
			scanner := bufio.NewScanner(cpuFile)
			if scanner.Scan() {
				for _, cpuStr := range strings.Split(scanner.Text(), ",") {
					if cpu, err := strconv.Atoi(cpuStr); err == nil {
						node.CPUs = append(node.CPUs, cpu)
					}
				}
			}
			cpuFile.Close()
		}
		
		// 读取内存信息
		if memFile, err := os.Open("/sys/devices/system/node/" + f.Name() + "/meminfo"); err == nil {
			scanner := bufio.NewScanner(memFile)
			for scanner.Scan() {
				if strings.Contains(scanner.Text(), "MemTotal") {
					fields := strings.Fields(scanner.Text())
					if len(fields) >= 4 {
						mem, _ := strconv.ParseUint(fields[3], 10, 64)
						node.Memory = mem * 1024 // 转换为字节
					}
				}
			}
			memFile.Close()
		}
		
		nodes = append(nodes, node)
	}
	return nodes, nil
}
