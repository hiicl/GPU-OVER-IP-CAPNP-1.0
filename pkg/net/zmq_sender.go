// ZMQ网络通信发送模块
// 负责将内存操作命令通过ZeroMQ发送到目标NUMA节点
package net

import (
	"fmt"
	"hash/crc32"
	"sync"

	"github.com/pebbe/zmq4"
	"github.com/hiicl/GPU-over-IP-AC922/proto/proto"
)

// 全局资源定义
var (
	zmqSocketMap   = make(map[string]*zmq4.Socket) // ZMQ套接字映射表（按IP地址）
	numaRouteTable = make(map[int]string)           // NUMA节点到IP地址的路由表
	mu             sync.RWMutex                    // 全局资源读写锁
)

// InitRouteTable 初始化NUMA节点到IP地址的路由表
// 参数：
//   table - NUMA节点ID到IP地址的映射表
func InitRouteTable(table map[int]string) {
	mu.Lock()
	defer mu.Unlock()
	numaRouteTable = table
}

// SendToRemoteNUMA 发送内存操作命令到指定NUMA节点
// 核心功能：
//   1. 根据NUMA节点ID查找目标IP地址
//   2. 建立或复用ZMQ套接字连接
//   3. 计算并添加数据校验和
//   4. 序列化并发送命令
// 参数：
//   numaID - 目标NUMA节点ID
//   cmd - 内存操作命令结构体
// 返回值：错误信息
func SendToRemoteNUMA(numaID int, cmd *proto.MemcopyCommand) error {
	mu.RLock()
	ip, exists := numaRouteTable[numaID]
	mu.RUnlock()
	
	if !exists {
		return fmt.Errorf("no route for NUMA node %d", numaID)
	}
	
	mu.Lock()
	defer mu.Unlock()
	
	socket, exists := zmqSocketMap[ip]
	if !exists {
		var err error
		socket, err = zmq4.NewSocket(zmq4.DGRAM)
		if err != nil {
			return fmt.Errorf("failed to create ZMQ socket: %w", err)
		}
		
		endpoint := fmt.Sprintf("udp://%s:5555", ip)
		if err := socket.Connect(endpoint); err != nil {
			return fmt.Errorf("failed to connect to %s: %w", endpoint, err)
		}
		
		// 优化发送设置
		if err := socket.SetSndhwm(1000); err != nil {
			return fmt.Errorf("failed to set send HWM: %w", err)
		}
		if err := socket.SetLinger(0); err != nil {
			return fmt.Errorf("failed to set linger: %w", err)
		}
		
		zmqSocketMap[ip] = socket
	}
	
	// 计算并设置校验和
	cmd.Checksum = crc32.ChecksumIEEE(cmd.Data)
	data, err := cmd.Marshal()
	if err != nil {
		return fmt.Errorf("failed to marshal command: %w", err)
	}
	
	if _, err := socket.SendBytes(data, 0); err != nil {
		return fmt.Errorf("failed to send data: %w", err)
	}
	
	return nil
}
