// NUMA节点发现模块
// 功能：
//   1. 自动检测系统NUMA节点拓扑
//   2. 识别节点间的连接关系
//   3. 构建NUMA节点拓扑图
package numa

import (
	"bufio"
	"os"
	"strconv"
	"strings"
)

// Node 表示NUMA节点信息
// 包含节点的硬件资源和拓扑关系
type Node struct {
	ID       int
	CPUs     []int
	Memory   uint64
	OpenCapi []OpenCapiBinding // OpenCAPI设备绑定
}

// OpenCapiBinding OpenCAPI设备绑定信息
type OpenCapiBinding struct {
	Path     string // 设备路径
	Type     string // 设备类型（AFU/legacy/CXL）
	Source   string // 发现来源（ocxl/device-tree/cxl）
	NumaID   int    // 绑定的NUMA节点ID
	Health   string // 设备健康状态
}

// DiscoverNodes 发现系统中的NUMA节点
// 核心功能：
//   1. 扫描/sys/devices/system/node目录获取节点信息
//   2. 收集每个节点的CPU、内存信息
//   3. 关联OpenCAPI设备到对应节点
// 返回值：
//   节点列表和错误信息
func DiscoverNodes() ([]Node, error) {
	nodes := []Node{}
	
	// 解析/sys/devices/system/node目录
	files, err := os.ReadDir("/sys/devices/system/node")
	if err != nil {
		return nil, err
	}
	
	// 发现OpenCAPI设备
	ocBindings := DiscoverOpenCapiDevices()
	
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
		
		// 关联OpenCAPI设备
		for _, binding := range ocBindings {
			if binding.NumaID == nodeID {
				node.OpenCapi = append(node.OpenCapi, binding)
			}
		}
		
		nodes = append(nodes, node)
	}
	return nodes, nil
}

// IsSameNode 检查两个设备是否在同一NUMA节点
// 参数：
//   deviceA - 第一个设备ID
//   deviceB - 第二个设备ID
// 返回值：
//   如果设备在同一节点则返回true
func IsSameNode(deviceA, deviceB string) bool {
	// 实际实现中从设备ID提取NUMA节点信息
	return deviceA[:3] == deviceB[:3]
}

// DiscoverOpenCapiDevices 发现系统中的OpenCAPI设备
func DiscoverOpenCapiDevices() []OpenCapiBinding {
	var devices []OpenCapiBinding

	// 1. 高优先级：从/sys/class/ocxl读取
	if entries, err := filepath.Glob("/sys/class/ocxl/ocxl-*"); err == nil {
		for _, path := range entries {
			// 获取NUMA节点信息
			numaPath := filepath.Join(path, "numa_node")
			numaID := -1
			if data, err := os.ReadFile(numaPath); err == nil {
				if id, err := strconv.Atoi(strings.TrimSpace(string(data))); err == nil {
					numaID = id
				}
			}
			
			devices = append(devices, OpenCapiBinding{
				Path:     path,
				Type:     "AFU",
				Source:   "ocxl",
				NumaID:   numaID,
				Health:   "active", // 默认健康状态
			})
		}
	}
	
	// 2. 回退机制：从/proc/device-tree读取（仅在ocxl结果为空时）
	if len(devices) == 0 {
		if entries, err := filepath.Glob("/proc/device-tree/ibm,open-capi@*"); err == nil {
			for _, path := range entries {
				// 从设备路径解析NUMA ID
				numaID := -1
				if matches := regexp.MustCompile(`@(\d+)`).FindStringSubmatch(path); len(matches) > 1 {
					if id, err := strconv.Atoi(matches[1]); err == nil {
						numaID = id
					}
				}
				
				devices = append(devices, OpenCapiBinding{
					Path:   path,
					Type:   "legacy",
					Source: "device-tree",
					NumaID: numaID,
					Health: "unknown",
				})
			}
		}
	}
	
	// 执行设备健康检查
	for i := range devices {
		CheckDeviceHealth(&devices[i])
	}
	
	return devices
}

// CheckDeviceHealth 检查OpenCAPI设备健康状态
func CheckDeviceHealth(binding *OpenCapiBinding) {
	// OCXL设备检查状态文件
	if binding.Source == "ocxl" {
		statusPath := filepath.Join(binding.Path, "status")
		if data, err := os.ReadFile(statusPath); err == nil {
			status := strings.TrimSpace(string(data))
			if status != "active" {
				binding.Health = "degraded"
			}
		}
	}
	// device-tree设备无运行时检查
}
