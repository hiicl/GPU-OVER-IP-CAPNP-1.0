// aitherion-cli 提供GPU-over-IP管理工具的命令行接口
package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"strconv"
	"syscall"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/container" // 添加容器模块导入
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/utils"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/numa" // 添加NUMA模块导入
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
)

// 命令行参数
var (
	controlIface string // 控制面网络接口
	dataIface    string // 数据面网络接口
	zmqPort      int    // ZMQ服务端口
)

var rootCmd = &cobra.Command{
	Use:   "aitherion-cli",
	Short: "GPU-over-IP aitherion-cli 管理工具",
	Long:  `管理GPU-over-IP Server的拓扑初始化与服务启动`,
	PersistentFlags: func() *pflag.FlagSet {
		flags := pflag.NewFlagSet("aitherion-cli", pflag.ContinueOnError)
		flags.StringVar(&controlIface, "control-iface", "eth0", "控制面网络接口")
		flags.StringVar(&dataIface, "data-iface", "eth1", "数据面网络接口")
		flags.IntVar(&zmqPort, "zmq-port", 5555, "ZMQ服务监听端口")
		return flags
	}(),
	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		// 启动独立的ZMQ receiver进程
		receiverCmd := exec.Command("zmq_receiver", 
			"--iface", dataIface, 
			"--port", strconv.Itoa(zmqPort))
		
		// 设置标准输出和错误输出
		receiverCmd.Stdout = os.Stdout
		receiverCmd.Stderr = os.Stderr
		
		// 启动receiver进程
		if err := receiverCmd.Start(); err != nil {
			log.Fatal("启动ZMQ receiver失败:", err)
		}
		log.Printf("[main] ZMQ receiver已启动，数据面接口: %s, 端口: %d", dataIface, zmqPort)

		// 将进程存储到上下文
		ctx := context.WithValue(cmd.Context(), "receiverProcess", receiverCmd.Process)
		cmd.SetContext(ctx)
	},
	PersistentPostRun: func(cmd *cobra.Command, args []string) {
		// 终止ZMQ receiver进程
		if process, ok := cmd.Context().Value("receiverProcess").(*os.Process); ok {
			if err := process.Kill(); err != nil {
				log.Printf("终止ZMQ receiver进程失败: %v", err)
			}
		}
	},
}

func main() {
	// 设置信号处理
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	
	// 创建根上下文
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	
	// 在goroutine中执行命令
	go func() {
		rootCmd.AddCommand(initCmd)
		rootCmd.AddCommand(cleanCmd) // 添加清理命令
		
		// 添加NUMA子命令
		numaCmd := &cobra.Command{
			Use:   "numa",
			Short: "NUMA节点管理",
		}
		numaCmd.AddCommand(numa.HealthCmd)
		numaCmd.AddCommand(numa.DiscoverCmd)
		numaCmd.AddCommand(numa.ConfigureCmd)
		numaCmd.AddCommand(numa.StartCmd)
		rootCmd.AddCommand(numaCmd)
		
		// 添加容器管理子命令
		containerCmd := &cobra.Command{
			Use:   "container",
			Short: "容器管理",
		}
		containerCmd.AddCommand(container.StartCmd)
		rootCmd.AddCommand(containerCmd)
		
		if err := rootCmd.ExecuteContext(ctx); err != nil {
			fmt.Println(err)
			os.Exit(1)
		}
	}()

	// 等待终止信号
	<-sigChan
	log.Println("接收到终止信号，清理资源...")
	
	// 取消上下文
	cancel()
	
	// 终止ZMQ receiver进程
	if process, ok := rootCmd.Context().Value("receiverProcess").(*os.Process); ok {
		if err := process.Kill(); err != nil {
			log.Printf("终止ZMQ receiver进程失败: %v", err)
		}
	}
	
	log.Println("程序已退出")
}
