package numa

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
)

// Driver 封装NUMA驱动操作
type Driver struct {
	mu sync.Mutex
}

func NewDriver() *Driver {
	return &Driver{}
}

// GetGPUNUMANode 获取GPU的NUMA节点
func (d *Driver) GetGPUNUMANode(gpuID int) (int, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	path := filepath.Join("/sys/class/drm/card"+strconv.Itoa(gpuID), "device/numa_node")
	content, err := os.ReadFile(path)
	if err != nil {
		return -1, err
	}
	
	numa, err := strconv.Atoi(strings.TrimSpace(string(content)))
	if err != nil {
		return -1, err
	}
	return numa, nil
}

// GetInterfaceNUMANode 获取网卡的NUMA节点  
func (d *Driver) GetInterfaceNUMANode(iface string) (int, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	path := filepath.Join("/sys/class/net", iface, "device/numa_node")
	content, err := os.ReadFile(path)
	if err != nil {
		return -1, err
	}
	
	numa, err := strconv.Atoi(strings.TrimSpace(string(content)))
	if err != nil {
		return -1, err
	}
	return numa, nil
}

// AcquireNUMANode 申请NUMA节点资源
func (d *Driver) AcquireNUMANode(node int) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("numactl", "--membind", strconv.Itoa(node), "echo", "1")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("绑定NUMA节点失败: %v", err)
	}
	return nil
}

// ReleaseNUMANode 释放NUMA节点资源
func (d *Driver) ReleaseNUMANode(node int) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("numactl", "--interleave=all", "echo", "1")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("释放NUMA绑定失败: %v", err)
	}
	return nil
}

// MonitorNUMANodes 监控NUMA节点状态
func (d *Driver) MonitorNUMANodes() (map[int]int, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("numastat", "-n")
	output, err := cmd.Output()
	if err != nil {
		return nil, err
	}

	status := make(map[int]int)
	lines := strings.Split(string(output), "\n")
	for _, line := range lines[1:] { // 跳过标题行
		fields := strings.Fields(line)
		if len(fields) < 2 {
			continue
		}
		node, _ := strconv.Atoi(fields[0])
		usage, _ := strconv.Atoi(fields[1])
		status[node] = usage
	}
	return status, nil
}
