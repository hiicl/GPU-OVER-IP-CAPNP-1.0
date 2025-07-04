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
// cfg: CLI配置参数
func StartContainers(cfg config.CLIConfig) error {
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
		args = append(args, "./capnpserver")
		args = append(args, "--port", strconv.Itoa(capnpPort))
		args = append(args, "--zmq-port", strconv.Itoa(cfg.ZmqBasePort))
		args = append(args, "--data-iface", cfg.DataIface) // 使用配置的数据面网卡
args = append(args, image)

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
