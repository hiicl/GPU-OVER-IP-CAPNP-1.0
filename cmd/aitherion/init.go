package main

import (
    "fmt"
    "github.com/hiicl/GPU-over-IP-AC922/cmd/aitherion/utils"
    "github.com/spf13/cobra"
)

var initCmd = &cobra.Command{
    Use:   "init",
    Short: "生成NUMA拓扑信息",
    Run: func(cmd *cobra.Command, args []string) {
        if err := utils.GenerateTopologyFiles(); err != nil {
            fmt.Println("拓扑生成失败:", err)
        } else {
            fmt.Println("✓ 拓扑生成成功 (/var/lib/aitherion/topology)")
        }
    },
}
