package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion/config"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion/utils"

	"github.com/spf13/cobra"
)

var cfg config.CLIConfig

var startCmd = &cobra.Command{
	Use:   "start",
	Short: "启动NUMA容器实例",
	Run: func(cmd *cobra.Command, args []string) {
		// 扫描NUMA拓扑文件
		numaFiles, err := filepath.Glob("/var/lib/aitherion/topology/numa[0-9]*_gpus.txt")
		if err != nil {
			fmt.Printf("扫描 NUMA 文件失败: %v\n", err)
			os.Exit(1)
		}
		if len(numaFiles) == 0 {
			fmt.Println("未检测到 NUMA GPU 拓扑文件，退出")
			os.Exit(1)
		}

		// 限制启动NUMA数量
		startNum := len(numaFiles)
		if cfg.NumNUMA > 0 && cfg.NumNUMA < startNum {
			startNum = cfg.NumNUMA
		}

		fmt.Printf("检测到 %d 个 NUMA 节点，将启动 %d 个 NUMA 容器\n", len(numaFiles), startNum)

		// 默认启用NUMA绑定
		if !cmd.Flags().Changed("no-numa-bind") {
			cfg.DisableNUMABinding = false
		}

		// 分配端口
		for i := 0; i < startNum; i++ {
			cfg.CapnpBasePort = cfg.CapnpBasePort + i
			cfg.CurrentNUMAIndex = i // 新增字段，便于 docker.go 使用（可选）

			fmt.Printf("[start] 启动 NUMA %d 容器，Cap'n Proto 端口: %d\n", i, cfg.CapnpBasePort)
			if err := utils.StartContainerForNUMA(cfg, i); err != nil {
				fmt.Printf("启动 NUMA %d 容器失败: %v\n", i, err)
				os.Exit(1)
			}
		}

		fmt.Println("[start] 所有 NUMA 容器启动成功")
	},
}

func init() {
	// Memext 相关参数
	startCmd.Flags().BoolVar(&cfg.EnableMemExt, "memext", false, "启用memext模块")
	startCmd.Flags().IntVar(&cfg.MemExtSizeMB, "memext-size-mb", 0, "memext内存大小(MB)，0表示自动计算")
	startCmd.Flags().Float64Var(&cfg.MemExtRatio, "memext-ratio", 0.9, "memext空闲内存使用比例(默认0.9)")
	startCmd.Flags().BoolVar(&cfg.DisableNUMABinding, "no-numa-bind", false, "禁用NUMA绑定")
	startCmd.Flags().BoolVar(&cfg.EnableHugePages, "hugepages", true, "启用临时巨页")

	startCmd.Flags().BoolVar(&cfg.EnableNetBalance, "netbalance", false, "启用网卡负载均衡")

	startCmd.Flags().IntVar(&cfg.CapnpBasePort, "capnp-port", 50051, "Cap'n Proto服务端口")
	startCmd.Flags().StringVar(&cfg.ImageName, "image", "aitherion-server", "容器镜像名称")
	startCmd.Flags().StringVar(&cfg.Tag, "tag", "latest", "镜像tag")
	startCmd.Flags().BoolVar(&cfg.DryRun, "dry-run", false, "仅打印命令不执行")

	startCmd.Flags().IntVar(&cfg.NumNUMA, "num-numa", 0, "启动NUMA容器数量，0表示全部")
	startCmd.Flags().StringVar(&cfg.ControlIface, "control-iface", "eth0", "控制面网卡接口名")
	startCmd.Flags().StringVar(&cfg.DataIface, "data-iface", "eth1", "数据面网卡接口名")
}
