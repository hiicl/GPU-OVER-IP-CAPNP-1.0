package nvidia

import (
	"fmt"
	"os/exec"
	"strings"
	"strconv"
	"sync"
)

// Driver 封装NVIDIA驱动操作
type Driver struct {
	mu sync.Mutex
}

func NewDriver() *Driver {
	return &Driver{}
}

// GPUInfo 包含GPU的基本信息
type GPUInfo struct {
	UUID      string
	Name      string
	MemoryMB  int
	Utilization int
}

// QueryGPUs 查询所有GPU信息
func (d *Driver) QueryGPUs() ([]GPUInfo, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("nvidia-smi",
		"--query-gpu=uuid,name,memory.total,utilization.gpu",
		"--format=csv,noheader,nounits")
	output, err := cmd.Output()
	if err != nil {
		return nil, err
	}

	var gpus []GPUInfo
	for _, line := range strings.Split(string(output), "\n") {
		fields := strings.Split(line, ",")
		if len(fields) < 4 {
			continue
		}

		mem, _ := strconv.Atoi(strings.TrimSpace(fields[2]))
		util, _ := strconv.Atoi(strings.TrimSpace(fields[3]))
		gpus = append(gpus, GPUInfo{
			UUID:      strings.TrimSpace(fields[0]),
			Name:      strings.TrimSpace(fields[1]),
			MemoryMB:  mem,
			Utilization: util,
		})
	}
	return gpus, nil
}

// GetTopology 获取GPU拓扑信息
func (d *Driver) GetTopology() (string, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("nvidia-smi", "topo", "-m")
	output, err := cmd.Output()
	return string(output), err
}

// AcquireGPU 申请GPU资源
func (d *Driver) AcquireGPU(uuid string) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("nvidia-smi", "-i", uuid, "--gom=0") // 设置计算模式为独占进程
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("设置GPU独占模式失败: %v", err)
	}
	return nil
}

// ReleaseGPU 释放GPU资源
func (d *Driver) ReleaseGPU(uuid string) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	cmd := exec.Command("nvidia-smi", "-i", uuid, "--gom=1") // 恢复默认计算模式
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("恢复GPU计算模式失败: %v", err)
	}
	return nil
}

// MonitorGPUs 监控GPU状态
func (d *Driver) MonitorGPUs() (map[string]int, error) {
	d.mu.Lock()
	defer d.mu.Unlock()
	gpus, err := d.QueryGPUs()
	if err != nil {
		return nil, err
	}

	status := make(map[string]int)
	for _, gpu := range gpus {
		status[gpu.UUID] = gpu.Utilization
	}
	return status, nil
}
