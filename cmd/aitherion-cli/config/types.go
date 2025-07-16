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
    ZmqBasePort     int    // ZMQ 数据传输基础端口
    ImageName      string // 容器镜像名
    Tag            string // 镜像 tag
    DryRun         bool   // 仅输出 docker 命令不执行
    
    NumNUMA         int // NUMA容器数量
    CurrentNUMAIndex int // 当前NUMA索引
    
    // NUMA 内存配置
    NumaId         int     // NUMA ID (0 or 1)
    MemPercent     float64 // 内存使用百分比 (1-90)
    VmemRatio      float64 // 显存占比 (0.1-0.9)
    
    // 网络接口参数
    ControlIface   string // 控制面网卡接口名
    DataIface      string // 数据面网卡接口名

    // Macvlan 配置
    NUMANICMap     map[string]string // NUMA节点网卡映射 (格式: node0:eth0,node1:eth1)
    EnableMacvlan  bool              // 启用macvlan网络隔离
    MacvlanMode    string            // macvlan模式: bridge/vepa/passthru
    
    // RDMA 设备配置
    RDMADevices   []RDMADevice       `yaml:"rdma_devices,omitempty"` // RDMA设备列表
}

// NumaConfig 存储NUMA节点配置
type NumaConfig struct {
	MemPercent   float64
	VmemRatio    float64
	ControlIface string
	DataIface    string
}

// RDMADevice 存储RDMA设备配置
type RDMADevice struct {
	PCIAddress  string `yaml:"pci_address"`
	Description string `yaml:"description"`
	InterfaceName string `yaml:"interface_name"` // 添加网口名称字段
	MacvlanBindings []MacvlanBinding `yaml:"macvlan_bindings,omitempty"`
	NumaNodes []int `yaml:"numa_nodes,omitempty"` // 添加NUMA节点绑定字段
}

// MacvlanBinding 存储macvlan绑定配置
type MacvlanBinding struct {
	Container string `yaml:"container"`
	Interface string `yaml:"interface"`
	IP        string `yaml:"ip"`
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
        ZmqBasePort:       5555,      // ZMQ 默认基础端口
        ImageName:        "aitherion-cli-server",
        Tag:              "latest",
        DryRun:           false,
        
        // 新增字段默认值
        NumNUMA:         0,
        CurrentNUMAIndex: 0,
        
        // 网络接口默认值
        ControlIface:   "eth0",
        DataIface:      "eth1",

        NUMANICMap:     make(map[string]string),
        EnableMacvlan:  false,
        MacvlanMode:    "bridge",
        
        // NUMA 内存配置默认值
        NumaId:        0,      // 默认NUMA ID 0
        MemPercent:     80.0,   // 默认内存使用80%
        VmemRatio:      0.9,    // 默认显存占比90%
    }
}
