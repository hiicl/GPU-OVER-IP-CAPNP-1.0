package utils

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
)

// StartContainers 启动NUMA节点容器集群
// cfg: 全局CLI配置
// nodeConfigs: NUMA节点配置映射
func StartContainers(cfg config.CLIConfig, nodeConfigs map[int]config.NUMAPconfig) error {
	numaDirs, err := filepath.Glob("/var/lib/aitherion-cli/topology/numa[0-9]*_gpus.txt")
	if err != nil || len(numaDirs) == 0 {
		return fmt.Errorf("无法读取 NUMA 拓扑文件: %v", err)
	}

	for i, gpuPath := range numaDirs {
		gpuBytes, _ := os.ReadFile(gpuPath)
		gpus := strings.TrimSpace(string(gpuBytes))
		if gpus == "" {
			fmt.Printf("[!] NUMA %d 无 GPU，跳过\n", i)
			continue
		}

	capnpPort := cfg.CapnpBasePort + i
	name := fmt.Sprintf("aitherion-cli-numa%d", i)

	// 创建macvlan网络接口（如果启用）
	if cfg.EnableMacvlan {
		if err := SetupMacvlanForNUMA(i, cfg); err != nil {
			return fmt.Errorf("为NUMA节点%d设置macvlan失败: %v", i, err)
		}
	}

		args := []string{
			"run", "-it", "--rm", "-d",
			"--runtime=nvidia",
		"-e", "NVIDIA_VISIBLE_DEVICES=" + gpus,
		"-e", fmt.Sprintf("CAPNP_PORT=%d", capnpPort),
		"-e", fmt.Sprintf("ZMQ_PORT=%d", cfg.ZmqBasePort),
		"-e", fmt.Sprintf("CONTROL_IFACE=%s", cfg.ControlIface), // 传递控制面网卡
		"-e", fmt.Sprintf("DATA_IFACE=%s", cfg.DataIface),       // 传递数据面网卡
		"-e", fmt.Sprintf("NUMA_NODE_ID=%d", i),                 // 新增：传递NUMA节点ID
		"-v", "/dev:/dev",
		"-p", fmt.Sprintf("%d:%d", capnpPort, capnpPort),
		"-p", fmt.Sprintf("%d:%d/udp", cfg.ZmqBasePort, cfg.ZmqBasePort),
		"--name", name,
		}

	// 挂载CUDA库
		if cudaLib := DetectCUDALib(); cudaLib != "" {
			args = append(args, "-v", fmt.Sprintf("%s:%s:ro", cudaLib, cudaLib))
		}

	// 挂载nvidia-smi
		if nsmi := DetectNvidiaSMI(); nsmi != "" {
			args = append(args, "-v", fmt.Sprintf("%s:%s:ro", nsmi, nsmi))
		}

	// 挂载网卡配置
ifacePath := fmt.Sprintf("/var/lib/aitherion-cli/topology/numa%d_iface.txt", i)
if ifaceBytes, err := os.ReadFile(ifacePath); err == nil {
    iface := strings.TrimSpace(string(ifaceBytes))
    if iface != "" {
        args = append(args, "-v", fmt.Sprintf("/sys/class/net/%s:/sys/class/net/%s:ro", iface, iface))
    }
}

		// 保留Cap'n Proto和ZMQ核心功能
	
		// 添加RDMA设备支持
		if len(cfg.RDMADevices) > 0 {
			for _, rdmaDev := range cfg.RDMADevices {
				// 检查设备是否绑定到当前NUMA节点
				for _, node := range rdmaDev.NUMANodes {
					if node == i {
						// 添加RDMA设备映射
						args = append(args, "--device", fmt.Sprintf("/dev/infiniband/%s", rdmaDev.InterfaceName))
						break
					}
				}
			}
		}
		
		// 添加macvlan网络绑定
		if cfg.EnableMacvlan {
			macvlanName := fmt.Sprintf("macvlan%d", i)
			args = append(args, "--network", fmt.Sprintf("container:%s", macvlanName))
		}

	// 绑定NUMA节点
		if !cfg.DisableNUMABinding {
			args = append(args, "--cpuset-mems", fmt.Sprintf("%d", i))
		}

		// 显存扩展支持
		if cfg.EnableMemExt {
			memPath := fmt.Sprintf("/mnt/memext/numa%d", i)
			os.MkdirAll(memPath, 0755)

		// 获取NUMA节点内存总量
			memFile := fmt.Sprintf("/sys/devices/system/node/node%d/meminfo", i)
			memKB := getNUMATotalMemoryKB(memFile)
			if memKB > 0 {
				// 分配 90%
				targetMB := memKB / 1024 * 90 / 100
				// 设置 hugepages 临时配置
				err := exec.Command("bash", "-c",
					fmt.Sprintf("echo %d > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages", targetMB/2),
				).Run()
				if err != nil {
					fmt.Printf("[!] NUMA %d 配置 hugepages 失败: %v\n", i, err)
				}
			}

			// 容器挂载共享内存目录
			args = append(args, "-v", fmt.Sprintf("%s:/mnt/memext", memPath))
		}

image := fmt.Sprintf("%s:%s", cfg.ImageName, cfg.Tag)
// 添加数据面接口参数
		// 获取节点特定配置
		nodeCfg, exists := nodeConfigs[i]
		if !exists {
			fmt.Printf("[!] NUMA %d 配置缺失，使用默认值\n", i)
			nodeCfg = config.NUMAPconfig{
				MemPercent: 80,
				VmemRatio: 0.8,
				ControlIface: "eth0",
				DataIface: "eth1",
			}
		}

		// 添加节点特定参数
		args = append(args, "./capnpserver")
		args = append(args, "--port", strconv.Itoa(capnpPort))
		args = append(args, "--zmq-port", strconv.Itoa(cfg.ZmqBasePort))
		args = append(args, "--control-iface", nodeCfg.ControlIface)
		args = append(args, "--data-iface", nodeCfg.DataIface)
args = append(args, image)

		// 添加RDMA特定环境变量
		if len(cfg.RDMADevices) > 0 {
			args = append(args, "-e", "ENABLE_RDMA=true")
			args = append(args, "-e", "RDMA_DEVICES="+getRDMADevicesForNUMA(i, cfg))
		}
		
		cmdLine := "docker " + strings.Join(args, " ")
		if cfg.DryRun {
			fmt.Println("[DryRun] " + cmdLine)
		} else {
			fmt.Println("[Run] " + cmdLine)
			cmd := exec.Command("docker", args...)
			cmd.Stdout = os.Stdout
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("NUMA %d 容器启动失败: %v", i, err)
			}
		}
	}

	fmt.Println("[✓] 所有 NUMA 容器启动完成 (使用 Cap'n Proto 协议)")
	return nil
}

// 获取绑定到指定NUMA节点的RDMA设备列表
func getRDMADevicesForNUMA(numaID int, cfg config.CLIConfig) string {
	var devices []string
	for _, rdmaDev := range cfg.RDMADevices {
		for _, node := range rdmaDev.NUMANodes {
			if node == numaID {
				devices = append(devices, rdmaDev.InterfaceName)
				break
			}
		}
	}
	return strings.Join(devices, ",")
}

// getNUMATotalMemoryKB 获取NUMA节点内存总量(kB)
// meminfoPath: 内存信息文件路径
func getNUMATotalMemoryKB(meminfoPath string) int {
	data, err := os.ReadFile(meminfoPath)
	if err != nil {
		return 0
	}
	lines := strings.Split(string(data), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "MemTotal:") {
			fields := strings.Fields(line)
			if len(fields) >= 2 {
				if kb, err := strconv.Atoi(fields[1]); err == nil {
					return kb
				}
			}
		}
	}
	return 0
}

// CheckContainerHealth 检查容器健康状态
func CheckContainerHealth(containerName string) error {
	// 检查容器是否正在运行
	cmd := exec.Command("docker", "inspect", "-f", "{{.State.Running}}", containerName)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("检查容器运行状态失败: %v", err)
	}

	if strings.TrimSpace(string(output)) != "true" {
		return errors.New("容器未运行")
	}

	// 检查OpenCAPI设备状态
	cmd = exec.Command("docker", "exec", containerName, "numa-tool", "check-ocxl-health")
	output, err = cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("OpenCAPI设备检查失败: %v\n输出: %s", err, string(output))
	}

	return nil
}

// SetupMacvlanForNUMA 为NUMA节点设置macvlan网络接口
func SetupMacvlanForNUMA(nodeID int, cfg config.CLIConfig) error {
	// 获取NUMA节点的物理网卡
	iface, err := GetPhysicalInterfaceForNUMA(nodeID)
	if err != nil {
		return fmt.Errorf("获取NUMA节点%d的物理网卡失败: %w", nodeID, err)
	}

	// 创建macvlan接口
	macvlanName := fmt.Sprintf("macvlan%d", nodeID)
	cmd := exec.Command("ip", "link", "add", macvlanName, "link", iface, "type", "macvlan", "mode", "bridge")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("创建macvlan接口失败: %v, 输出: %s", err, string(output))
	}

	// 启动macvlan接口
	cmd = exec.Command("ip", "link", "set", macvlanName, "up")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("启动macvlan接口失败: %v, 输出: %s", err, string(output))
	}

	// 创建网络命名空间
	netns := fmt.Sprintf("aitherion-ns%d", nodeID)
	cmd = exec.Command("ip", "netns", "add", netns)
	if output, err := cmd.CombinedOutput(); err != nil {
		// 如果已存在则忽略
		if !strings.Contains(string(output), "already exists") {
			return fmt.Errorf("创建网络命名空间失败: %v, 输出: %s", err, string(output))
		}
	}

	// 将macvlan接口移到网络命名空间
	cmd = exec.Command("ip", "link", "set", macvlanName, "netns", netns)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("移动macvlan到命名空间失败: %v, 输出: %s", err, string(output))
	}

	// 在命名空间中配置macvlan接口
	cmd = exec.Command("ip", "netns", "exec", netns, "ip", "link", "set", macvlanName, "up")
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("在命名空间中启动macvlan失败: %v, 输出: %s", err, string(output))
	}

	// 分配IP地址（从配置中获取子网信息）
	ipAddr := fmt.Sprintf("192.168.%d.2/24", nodeID) // 实际使用中应从配置获取
	cmd = exec.Command("ip", "netns", "exec", netns, "ip", "addr", "add", ipAddr, "dev", macvlanName)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("分配IP地址失败: %v, 输出: %s", err, string(output))
	}

	// 设置默认路由
	gateway := fmt.Sprintf("192.168.%d.1", nodeID) // 实际使用中应从配置获取
	cmd = exec.Command("ip", "netns", "exec", netns, "ip", "route", "add", "default", "via", gateway)
	if output, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("设置默认路由失败: %v, 输出: %s", err, string(output))
	}

	fmt.Printf("NUMA节点%d的macvlan网络配置完成: %s@%s\n", nodeID, macvlanName, netns)
	return nil
}

// GetPhysicalInterfaceForNUMA 获取NUMA节点绑定的物理网卡
func GetPhysicalInterfaceForNUMA(nodeID int) (string, error) {
	// 实际实现应根据系统配置获取NUMA绑定的网卡
	// 这里简化为返回eth0或eth1
	if nodeID == 0 {
		return "eth0", nil
	}
	return "eth1", nil
}
