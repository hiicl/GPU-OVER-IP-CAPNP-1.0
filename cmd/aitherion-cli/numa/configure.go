package numa

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
	"github.com/spf13/cobra"
)

// NUMAPconfig 存储NUMA节点配置
type NUMAPconfig struct {
	MemPercent float64
	VmemRatio  float64
	ControlIface string
	DataIface   string
}

// 全局配置存储
var numaConfig = map[int]NUMAPconfig{
	0: {MemPercent: 80, VmemRatio: 0.8, ControlIface: "eth0", DataIface: "eth1"},
	1: {MemPercent: 80, VmemRatio: 0.8, ControlIface: "eth0", DataIface: "eth1"},
}

var ConfigureCmd = &cobra.Command{
	Use:   "configure",
	Short: "交互式配置NUMA节点参数",
	Run: func(cmd *cobra.Command, args []string) {
		mainMenu()
	},
}

func mainMenu() {
	reader := bufio.NewReader(os.Stdin)
	
	for {
		fmt.Println("\n=== NUMA配置菜单 ===")
		fmt.Println("[1] 配置NUMA0")
		fmt.Println("[2] 配置NUMA1")
		fmt.Println("[3] 执行start命令启动容器")
		fmt.Println("[4] 返回主菜单")
		fmt.Print("请选择操作: ")
		
		input, _ := reader.ReadString('\n')
		choice, err := strconv.Atoi(strings.TrimSpace(input))
		if err != nil {
			fmt.Println("无效输入，请输入数字选项")
			continue
		}
		
		switch choice {
		case 1:
			configureNUMANode(0)
		case 2:
			configureNUMANode(1)
		case 3:
			StartCmd.Run(cmd, args)
		case 4:
			return
		default:
			fmt.Println("无效选项，请重新选择")
		}
	}
}

func configureNUMANode(nodeID int) {
	cfg := numaConfig[nodeID]
	reader := bufio.NewReader(os.Stdin)
	
	for {
		fmt.Printf("\n=== 配置NUMA%d ===\n", nodeID)
		fmt.Printf("当前设置:\n  内存百分比: %.1f%%\n  显存占比: %.1f\n  控制面接口: %s\n  数据面接口: %s\n",
			cfg.MemPercent, cfg.VmemRatio, cfg.ControlIface, cfg.DataIface)
		fmt.Println("[1] 修改内存百分比")
		fmt.Println("[2] 修改显存占比")
		fmt.Println("[3] 修改网络接口")
		fmt.Println("[4] 保存并返回")
		fmt.Print("请选择操作: ")
		
		input, _ := reader.ReadString('\n')
		choice, err := strconv.Atoi(strings.TrimSpace(input))
		if err != nil {
			fmt.Println("无效输入，请输入数字选项")
			continue
		}
		
		switch choice {
		case 1:
			fmt.Print("请输入新的内存百分比 (1-90): ")
			input, _ = reader.ReadString('\n')
			value, err := strconv.ParseFloat(strings.TrimSpace(input), 64)
			if err == nil && value > 0 && value <= 90 {
				cfg.MemPercent = value
			} else {
				fmt.Println("无效的内存百分比值")
			}
		case 2:
			fmt.Print("请输入新的显存占比 (0.1-0.9): ")
			input, _ = reader.ReadString('\n')
			value, err := strconv.ParseFloat(strings.TrimSpace(input), 64)
			if err == nil && value >= 0.1 && value <= 0.9 {
				cfg.VmemRatio = value
			} else {
				fmt.Println("无效的显存占比值")
			}
		case 3:
			fmt.Print("请输入控制面接口名称: ")
			cfg.ControlIface, _ = reader.ReadString('\n')
			cfg.ControlIface = strings.TrimSpace(cfg.ControlIface)
			fmt.Print("请输入数据面接口名称: ")
			cfg.DataIface, _ = reader.ReadString('\n')
			cfg.DataIface = strings.TrimSpace(cfg.DataIface)
		case 4:
			numaConfig[nodeID] = cfg
			return
		default:
			fmt.Println("无效选项，请重新选择")
		}
	}
}
