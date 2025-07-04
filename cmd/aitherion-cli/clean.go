package main

import (
	"fmt"
	"strconv"

	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/utils"
	"github.com/spf13/cobra"
)

var cleanCmd = &cobra.Command{
	Use:   "clean",
	Short: "清理macvlan网络接口",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("清理macvlan网络接口...")
		
		// 清理所有可能的NUMA节点接口（0-7）
		for i := 0; i < 8; i++ {
			if err := utils.CleanupMacvlanForNUMA(i); err != nil {
				fmt.Printf("NUMA %d 清理失败: %v\n", i, err)
			} else {
				fmt.Printf("NUMA %d 接口已清理\n", i)
			}
		}
		fmt.Println("所有macvlan接口清理完成")
	},
}

func init() {
	rootCmd.AddCommand(cleanCmd)
}
