package numa

import (
	"fmt"
	"github.com/spf13/cobra"
	"github.com/hiicl/GPU-over-IP-AC922/pkg/numa"
)

var DiscoverCmd = &cobra.Command{
	Use:   "discover",
	Short: "自动检测系统NUMA拓扑",
	Run: func(cmd *cobra.Command, args []string) {
		fmt.Println("正在检测NUMA拓扑...")
		
		// 检测NUMA节点
		nodes, err := numa.DetectNodes()
		if err != nil {
			fmt.Printf("NUMA检测失败: %v\n", err)
			return
		}
		
		fmt.Printf("检测到 %d 个NUMA节点:\n", len(nodes))
		for i, node := range nodes {
			fmt.Printf("NUMA%d: CPU核心数=%d, GPU数量=%d, 网卡数量=%d\n", 
				i, node.CPUCores, len(node.GPUs), len(node.NetworkDevices))
			
			// 打印GPU-NUMA-网卡关系
			fmt.Printf("  GPU设备: %v\n", node.GPUs)
			fmt.Printf("  关联网卡: %v\n", node.NetworkDevices)
			if len(node.RDMADevices) > 0 {
				fmt.Printf("  RDMA设备: %v\n", node.RDMADevices)
			}
		}
		
		fmt.Println("NUMA拓扑检测完成")
	},
}
