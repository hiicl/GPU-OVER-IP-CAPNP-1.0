package config

// CLIConfig 定义 aitherion CLI 配置参数
type CLIConfig struct {
	// MemExt 模块参数
	EnableMemExt       bool    // 启用memext模块
	MemExtSizeMB       int     // 共享显存池大小(MB)，0表示使用比率
	MemExtRatio        float64 // 空闲内存使用比率(默认0.9)
	EnableHugePages    bool    // 启用临时hugepages
	DisableNUMABinding bool    // 禁用NUMA绑定

	// NetBalance 模块参数
	EnableNetBalance bool // 是否启用网卡 NUMA 负载均衡

	// Docker 启动参数
	CapnpBasePort   int    // Cap'n Proto 服务端口
	ImageName      string // 容器镜像名
	Tag            string // 镜像 tag
	DryRun         bool   // 仅输出 docker 命令不执行
	
	NumNUMA         int // NUMA容器数量
	CurrentNUMAIndex int // 当前NUMA索引
	
	// 网络接口参数
	ControlIface   string // 控制面网卡接口名
	DataIface      string // 数据面网卡接口名
}

// 默认值（也可扩展 defaults.go 加载）
func DefaultCLIConfig() CLIConfig {
	return CLIConfig{
		EnableMemExt:       false,
		MemExtSizeMB:       0,         // 若为 0，则使用 MemExtRatio
		MemExtRatio:        0.9,       // 默认使用 90% 的空闲内存
		EnableHugePages:    true,      // 默认启用临时 hugepages
		DisableNUMABinding: false,     // 默认绑定 NUMA

		EnableNetBalance: false,
		CapnpBasePort:     50051,
		ImageName:        "aitherion-server",
		Tag:              "latest",
		DryRun:           false,
		
		// 新增字段默认值
		NumNUMA:         0,
		CurrentNUMAIndex: 0,
		
		// 网络接口默认值
		ControlIface:   "eth0",
		DataIface:      "eth1",
	}
}
