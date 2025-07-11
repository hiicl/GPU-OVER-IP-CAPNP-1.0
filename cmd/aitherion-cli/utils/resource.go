package utils

import (
	"errors"
	"fmt"
	"os/exec"
	"strconv"
	"strings"
)

// NUMAMemoryInfo 存储NUMA节点内存信息
type NUMAMemoryInfo struct {
	TotalMB int
	FreeMB  int
}

// GetNUMAMemory 获取指定NUMA节点的内存信息
func GetNUMAMemory(nodeID int) (NUMAMemoryInfo, error) {
	cmd := exec.Command("numactl", "-H")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return NUMAMemoryInfo{}, fmt.Errorf("执行numactl失败: %v", err)
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.Contains(line, fmt.Sprintf("node %d size", nodeID)) {
			fields := strings.Fields(line)
			if len(fields) < 4 {
				return NUMAMemoryInfo{}, fmt.Errorf("解析numactl输出失败: %s", line)
			}

			totalMB, err := strconv.Atoi(fields[3])
			if err != nil {
				return NUMAMemoryInfo{}, fmt.Errorf("解析内存大小失败: %v", err)
			}

			return NUMAMemoryInfo{TotalMB: totalMB}, nil
		}
	}

	return NUMAMemoryInfo{}, fmt.Errorf("未找到NUMA节点%d的内存信息", nodeID)
}

// CalculateMemoryLimit 计算NUMA节点的内存限制
func CalculateMemoryLimit(totalMemMB int, percent float64) (int, error) {
	if percent > 90 {
		return 0, errors.New("内存百分比不能超过90%")
	}
	limit := int(float64(totalMemMB)*percent/100) - 1024
	if limit < 1024 {
		return 0, errors.New("计算后内存限制过低")
	}
	return limit, nil
}
