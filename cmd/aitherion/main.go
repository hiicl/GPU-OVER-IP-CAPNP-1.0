// aitherion 提供GPU-over-IP管理工具的命令行接口
package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion/config"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion/utils"
	"github.com/hiicl/GPU-over-IP-AC922/modules/memext"
	"github.com/hiicl/GPU-over-IP-AC922/pkg/roce"
	"github.com/spf13/cobra"
	"github.com/spf13/pflag"
)

// 命令行参数
var (
	controlIface string // 控制面网络接口
	dataIface    string // 数据面网络接口
)

var rootCmd = &cobra.Command{
	Use:   "aitherion",
	Short: "aitherion CLI 管理工具",
	Long:  `管理aitherion Server的拓扑初始化与服务启动`,
	PersistentFlags: func() *pflag.FlagSet {
		flags := pflag.NewFlagSet("aitherion", pflag.ContinueOnError)
		flags.StringVar(&controlIface, "control-iface", "eth0", "控制面网络接口")
		flags.StringVar(&dataIface, "data-iface", "ib0", "数据面网络接口 (用于RoCE)")
		return flags
	}(),
	PersistentPreRun: func(cmd *cobra.Command, args []string) {
		// 初始化内存管理
		if err := memext.Init(true); err != nil {
			log.Fatal("内存初始化失败:", err)
		}

		// 验证数据面接口
		if dataIface == "" {
			log.Fatal("必须指定数据面网络接口 (--data-iface)")
		}
		
		// 创建RoCE连接
		roceConfig := &roce.RoCEConfig{
			Iface:     dataIface, // 使用命令行参数指定的数据面网卡
			Port:      4791,      // 默认RoCE端口
			MaxSendWr: 1024,
			MaxRecvWr: 1024,
			MaxInline: 256,
		}
		conn := roce.NewRoCEConnection()
		if err := conn.Init(roceConfig); err != nil {
			log.Fatal("RoCE初始化失败:", err)
		}

		// 注册内存
		if err := memext.RegisterWithRoCE(conn); err != nil {
			log.Fatal("内存注册失败:", err)
		}
		
		// 将连接存储到上下文
		ctx := context.WithValue(cmd.Context(), "roceConn", conn)
		cmd.SetContext(ctx)
		
		log.Printf("[main] 控制面接口: %s, 数据面接口: %s", controlIface, dataIface)
		
		// TODO: 实际使用控制面接口
		// 此处应添加控制面接口的初始化逻辑
		// 例如: net.Listen("tcp", controlIface+":8080")
	},
	PersistentPostRun: func(cmd *cobra.Command, args []string) {
		// 清理资源
		if conn, ok := cmd.Context().Value("roceConn").(roce.RoCEConnection); ok {
			if err := memext.Close(conn); err != nil {
				log.Printf("内存清理失败: %v", err)
			}
			if err := conn.Close(); err != nil {
				log.Printf("RoCE连接关闭失败: %v", err)
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
	
	// 通过命令上下文获取连接进行清理
	if cmdConn, ok := rootCmd.Context().Value("roceConn").(roce.RoCEConnection); ok {
		if err := memext.Close(cmdConn); err != nil {
			log.Printf("内存清理失败: %v", err)
		}
		if err := cmdConn.Close(); err != nil {
			log.Printf("RoCE连接关闭失败: %v", err)
		}
	}
	
	log.Println("程序已退出")
}
