// aitherion-cli 提供GPU-over-IP管理工具的命令行接口
package main

import (
	"context"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/utils"
	"github.com/pebbe/zmq4"
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
)

// 命令行参数
var (
	controlIface string // 控制面网络接口
	zmqPort      int    // ZMQ服务端口
)

var rootCmd = &cobra.Command{
	Use:   "aitherion-cli",
	Short: "GPU-over-IP aitherion-cli 管理工具",
	Long:  `管理GPU-over-IP Server的拓扑初始化与服务启动`,
	PersistentFlags: func() *pflag.FlagSet {
		flags := pflag.NewFlagSet("aitherion-cli", pflag.ContinueOnError)
		flags.StringVar(&controlIface, "control-iface", "eth0", "控制面网络接口")
		flags.IntVar(&zmqPort, "zmq-port", 5555, "ZMQ服务监听端口")
		return flags
	}(),
	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		// 创建ZMQ上下文
		zmqCtx, err := zmq4.NewContext()
		if err != nil {
			log.Fatal("ZMQ上下文创建失败:", err)
		}

		// 创建ZMQ ROUTER套接字
		socket, err := zmqCtx.NewSocket(zmq4.ROUTER)
		if err != nil {
			log.Fatal("ZMQ套接字创建失败:", err)
		}

		// 绑定到控制面接口
		bindAddr := fmt.Sprintf("tcp://%s:%d", controlIface, zmqPort)
		if err := socket.Bind(bindAddr); err != nil {
			log.Fatalf("ZMQ绑定失败(%s): %v", bindAddr, err)
		}

		// 将套接字存储到上下文
		ctx := context.WithValue(cmd.Context(), "zmqSocket", socket)
		cmd.SetContext(ctx)
		
		log.Printf("[main] ZMQ服务已启动: %s", bindAddr)
		

	},
	PersistentPostRun: func(cmd *cobra.Command, args []string) {
		// 清理资源
		if socket, ok := cmd.Context().Value("zmqSocket").(*zmq4.Socket); ok {
			if err := socket.Close(); err != nil {
				log.Printf("ZMQ套接字关闭失败: %v", err)
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
		rootCmd.AddCommand(startCmd)
		rootCmd.AddCommand(cleanCmd) // 添加清理命令
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
	
		// 通过命令上下文获取ZMQ套接字进行清理
		if socket, ok := rootCmd.Context().Value("zmqSocket").(*zmq4.Socket); ok {
			if err := socket.Close(); err != nil {
				log.Printf("ZMQ套接字关闭失败: %v", err)
			}
		}
	
	log.Println("程序已退出")
}
