package net

import (
	"fmt"
	"sync"

	"github.com/pebbe/zmq4"
	"github.com/hiicl/GPU-over-IP-AC922/proto/proto"
)

var (
	zmqSocketMap   = make(map[string]*zmq4.Socket)
	numaRouteTable = make(map[int]string)
	mu             sync.RWMutex
)

// InitRouteTable 初始化NUMA节点到IP的映射表
func InitRouteTable(table map[int]string) {
	mu.Lock()
	defer mu.Unlock()
	numaRouteTable = table
}

// SendToRemoteNUMA 发送内存操作命令到指定NUMA节点
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
	
	data, err := cmd.Marshal()
	if err != nil {
		return fmt.Errorf("failed to marshal command: %w", err)
	}
	
	if _, err := socket.SendBytes(data, 0); err != nil {
		return fmt.Errorf("failed to send data: %w", err)
	}
	
	return nil
}
