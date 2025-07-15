package numa

import (
	"bufio"
	"fmt"
	"os"
	"os/exec" // 添加exec包用于执行命令
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
		fmt.Println("[4] ROCE网卡管理")  // 新增ROCE管理选项
		fmt.Println("[5] 返回主菜单")
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
			configureRoce()  // 调用ROCE配置函数
		case 5:
			return
		default:
			fmt.Println("无效选项，请重新选择")
		}
	}
}

// 新增ROCE网卡配置函数
func configureRoce() {
	reader := bufio.NewReader(os.Stdin)
	
	for {
		fmt.Println("\n=== ROCE网卡管理 ===")
		fmt.Println("[1] 添加ROCE网口")
		fmt.Println("[2] 配置macvlan")
		fmt.Println("[3] 绑定容器")
		fmt.Println("[4] 测试RDMA性能")
		fmt.Println("[5] 返回上级菜单")
		fmt.Print("请选择操作: ")
		
		input, _ := reader.ReadString('\n')
		choice, err := strconv.Atoi(strings.TrimSpace(input))
		if err != nil {
			fmt.Println("无效输入，请输入数字选项")
			continue
		}
		
		switch choice {
		case 1:
			addRoceInterface()
		case 2:
			configMacvlan()
		case 3:
			bindContainer()
		case 4:
			testRdma()
		case 5:
			return
		default:
			fmt.Println("无效选项，请重新选择")
		}
	}
}

// 添加ROCE网口
func addRoceInterface() {
	// 检测RDMA设备
	fmt.Println("正在检测支持ROCE/IB的网卡...")
	cmd := exec.Command("lspci", "-nn")
	output, err := cmd.Output()
	if err != nil {
		fmt.Printf("检测失败: %v\n", err)
		return
	}
	
	// 解析支持RDMA的设备
	scanner := bufio.NewScanner(strings.NewReader(string(output)))
	var devices []string
	for scanner.Scan() {
		line := scanner.Text()
		if strings.Contains(line, "InfiniBand") || 
		   strings.Contains(line, "ROCE") || 
		   strings.Contains(line, "RDMA") {
			devices = append(devices, line)
		}
	}
	
	if len(devices) == 0 {
		fmt.Println("未检测到支持ROCE/IB的网卡")
		return
	}
	
	fmt.Println("\n检测到以下支持ROCE/IB的网卡：")
	for i, dev := range devices {
		fmt.Printf("[%d] %s\n", i+1, dev)
	}
	
	reader := bufio.NewReader(os.Stdin)
	fmt.Print("\n请选择要添加的网卡 (1-", len(devices), "): ")
	input, _ := reader.ReadString('\n')
	index, err := strconv.Atoi(strings.TrimSpace(input))
	if err != nil || index < 1 || index > len(devices) {
		fmt.Println("无效选择")
		return
	}
	
	selected := devices[index-1]
	parts := strings.Fields(selected)
	if len(parts) > 0 {
		pciAddr := parts[0]
		
		// 新增：验证网口NUMA位置
		fmt.Printf("正在验证网口 %s 的NUMA位置...\n", pciAddr)
		cmd = exec.Command("sh", "-c", "lspci -v -s "+pciAddr+" | grep NUMA")
		output, err = cmd.Output()
		if err != nil {
			fmt.Printf("NUMA位置检测失败: %v\n", err)
		} else {
			fmt.Printf("检测到NUMA位置: %s\n", string(output))
		}
		
		fmt.Printf("已添加网口: %s\n", pciAddr)
		fmt.Println("选择绑定的NUMA节点 (可多选, 用逗号分隔):")
		fmt.Println("0: NUMA0")
		fmt.Println("1: NUMA1")
		fmt.Print("> ")
		input, _ = reader.ReadString('\n')
		numaNodes := strings.Split(strings.TrimSpace(input), ",")
		fmt.Printf("绑定到NUMA节点: %v\n", numaNodes)
		
		// 将设备添加到全局配置
		var numaInts []int
		for _, node := range numaNodes {
			if n, err := strconv.Atoi(node); err == nil {
				numaInts = append(numaInts, n)
			}
		}
		
		// 创建RDMA设备配置
		rdmaDevice := config.RDMADevice{
			PCIAddress:  pciAddr,
			Description: selected,
			NUMANodes:   numaInts,
		}
		
		// 添加到全局配置
		GlobalConfig.RDMADevices = append(GlobalConfig.RDMADevices, rdmaDevice)
		fmt.Printf("已保存配置: %+v\n", rdmaDevice)
	}
}

func configMacvlan() {
	fmt.Println("macvlan配置功能待实现")
}

func bindContainer() {
	fmt.Println("容器绑定功能待实现")
}

func testRdma() {
	fmt.Println("RDMA性能测试功能待实现")
}

// 获取全局配置
var GlobalConfig = config.DefaultCLIConfig()

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
