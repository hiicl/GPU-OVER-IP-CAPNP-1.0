package utils

import (
	"fmt"
	"os/exec"
	"strconv"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
)

// SetupMacvlanForNUMA 为指定NUMA节点创建macvlan接口
func SetupMacvlanForNUMA(numaIndex int, cfg config.CLIConfig) error {
	// 根据NUMA节点索引获取对应的网卡名称
	nodeKey := strconv.Itoa(numaIndex)
	nic, exists := cfg.NUMANICMap[nodeKey]
	if !exists {
		return fmt.Errorf("未找到NUMA节点 %d 的网卡映射", numaIndex)
	}

	macvlanName := fmt.Sprintf("macvlan%d", numaIndex)

	// 创建macvlan接口
	cmd := exec.Command("ip", "link", "add", macvlanName, "link", nic, "type", "macvlan", "mode", cfg.MacvlanMode)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("创建macvlan失败: %v, 输出: %s", err, string(output))
	}

	// 启动macvlan接口
	cmd = exec.Command("ip", "link", "set", macvlanName, "up")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("启动macvlan接口失败: %v, 输出: %s", err, string(output))
	}

	return nil
}

// CleanupMacvlanForNUMA 清理指定NUMA节点的macvlan接口
func CleanupMacvlanForNUMA(numaIndex int) error {
	macvlanName := fmt.Sprintf("macvlan%d", numaIndex)
	cmd := exec.Command("ip", "link", "delete", macvlanName)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("删除macvlan接口失败: %v, 输出: %s", err, string(output))
	}
	return nil
}
