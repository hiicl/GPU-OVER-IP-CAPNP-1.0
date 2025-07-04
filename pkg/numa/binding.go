package numa

import (
	"os"
	"strconv"
	"syscall"
)

// BindDeviceToNode 将PCI设备绑定到特定NUMA节点
func BindDeviceToNode(deviceID string, nodeID int) error {
	path := "/sys/bus/pci/devices/" + deviceID + "/numa_node"
	return os.WriteFile(path, []byte(strconv.Itoa(nodeID)), 0644)
}

// BindProcessToNode 将进程绑定到特定NUMA节点
func BindProcessToNode(pid int, nodeID int) error {
	// 设置CPU亲和性掩码
	var mask syscall.CPUSet
	mask.Set(nodeID)
	
	return syscall.SchedSetaffinity(pid, &mask)
}

// GetCurrentNode 获取当前进程所在的NUMA节点
func GetCurrentNode() (int, error) {
	var mask syscall.CPUSet
	if err := syscall.SchedGetaffinity(0, &mask); err != nil {
		return -1, err
	}
	
	for i := 0; i < mask.Count(); i++ {
		if mask.IsSet(i) {
			return i, nil
		}
	}
	return -1, nil
}
