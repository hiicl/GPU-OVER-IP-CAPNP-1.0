package numa

import (
	"fmt"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/config"
	"github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion-cli/utils"
	"github.com/spf13/cobra"
)

var StartCmd = &cobra.Command{
	Use:   "start",
	Short: "启动NUMA容器实例",
	Run: func(cmd *cobra.Command, args []string) {
		// 准备全局配置（使用默认值）
		globalCfg := config.CLIConfig{
			ImageName: "aitherion-runtime",
			Tag:       "latest",
			EnableMacvlan: true,
			MacvlanMode: "bridge",
			CapnpBasePort: 50051,
			ZmqBasePort: 5555,
		}

		// 调用统一的容器启动函数
		if err := utils.StartContainers(globalCfg, numaConfig); err != nil {
			fmt.Printf("容器启动失败: %v\n", err)
		} else {
			fmt.Println("所有NUMA容器启动完成")
		}
	},
}
