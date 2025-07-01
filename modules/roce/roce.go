package roce

import (
	"fmt"
	"net"
)

// RoCEConfig 定义RoCE连接配置
type RoCEConfig struct {
	Iface       string // 网卡接口名
	Port        int    // 通信端口
	MaxSendWr   int    // 最大发送请求数
	MaxRecvWr   int    // 最大接收请求数
	MaxInline   int    // 最大内联数据大小
}

// RoCEConnection 表示RoCE数据面连接
type RoCEConnection interface {
	Init(config *RoCEConfig) error
	RegisterMemory(addr uintptr, size uint) error
	Send(data []byte) error
	Receive() ([]byte, error)
	Close() error
}

// NewRoCEConnection 创建RoCE连接实例
func NewRoCEConnection() RoCEConnection {
	return &UCXConnection{}
}

// ValidateInterface 验证网卡支持RDMA
func ValidateInterface(iface string) error {
	ifaceObj, err := net.InterfaceByName(iface)
	if err != nil {
		return fmt.Errorf("网卡接口 %s 不存在: %v", iface, err)
	}
	
	if ifaceObj.Flags&net.FlagUp == 0 {
		return fmt.Errorf("网卡接口 %s 未启用", iface)
	}
	
	return nil
}
