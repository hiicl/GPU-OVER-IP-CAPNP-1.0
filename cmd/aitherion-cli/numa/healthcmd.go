package numa

import (
	"fmt"
	"os"
	"strconv"

	"github.com/olekukonko/tablewriter"
	"aitherion/numa"
	"github.com/spf13/cobra"
)

var HealthCmd = &cobra.Command{
	Use:   "health",
	Short: "检查OpenCAPI设备健康状态",
	Run: func(cmd *cobra.Command, args []string) {
		// 发现OpenCAPI设备
		devices := numa.DiscoverOpenCapiDevices()
		
		// 创建表格
		table := tablewriter.NewWriter(os.Stdout)
		table.SetHeader([]string{"设备路径", "类型", "源", "NUMA节点", "健康状态"})
		
		// 填充表格数据
		for _, dev := range devices {
			table.Append([]string{
				dev.Path,
				dev.Type,
				dev.Source,
				strconv.Itoa(dev.NumaID),
				dev.Health,
			})
		}
		
		// 渲染表格
		table.Render()
		
		// 检查是否有不健康设备
		hasDegraded := false
		for _, dev := range devices {
			if dev.Health == "degraded" {
				hasDegraded = true
				break
			}
		}
		
		if hasDegraded {
			fmt.Println("\n警告：检测到不健康的OpenCAPI设备，请检查日志获取详细信息")
			os.Exit(1)
		}
	},
}
