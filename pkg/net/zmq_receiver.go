// ZMQ网络通信接收模块
// 负责接收并处理来自其他NUMA节点的内存操作命令
package net

import (
	"context"
	"hash/crc32"
	"log"

	"github.com/pebbe/zmq4"
	"github.com/hiicl/GPU-over-IP-AC922/pkg/numa"
	"github.com/hiicl/GPU-over-IP-AC922/proto/proto"
)

// StartReceiver 启动ZMQ接收服务
// 核心功能：
//   1. 绑定UDP端口监听内存操作命令
//   2. 验证数据完整性（CRC32校验和）
//   3. 路由到本地NUMA内存访问模块处理
// 参数：
//   ctx - 上下文对象，用于优雅停止服务
func StartReceiver(ctx context.Context) {
	socket, err := zmq4.NewSocket(zmq4.DGRAM)
	if err != nil {
		log.Fatalf("Failed to create ZMQ receiver socket: %v", err)
	}
	defer socket.Close()

	if err := socket.Bind("udp://*:5555"); err != nil {
		log.Fatalf("Failed to bind ZMQ receiver: %v", err)
	}

	log.Println("ZMQ receiver started on port 5555")

	// 优化接收设置
	if err := socket.SetRcvhwm(1000); err != nil {
		log.Printf("Failed to set receive HWM: %v", err)
	}
	if err := socket.SetLinger(0); err != nil {
		log.Printf("Failed to set linger: %v", err)
	}

	for {
		select {
		case <-ctx.Done():
			log.Println("ZMQ receiver shutting down")
			return
		default:
			msg, err := socket.RecvBytes(0)
			if err != nil {
				log.Printf("Error receiving message: %v", err)
				continue
			}

			cmd, err := proto.UnmarshalMemcopyCommand(msg)
			if err != nil {
				log.Printf("Failed to unmarshal command: %v", err)
				continue
			}
			
			// 验证数据完整性
			calculatedChecksum := crc32.ChecksumIEEE(cmd.Data)
			if calculatedChecksum != cmd.Checksum {
				log.Printf("Data corruption detected! Expected: %d, Got: %d", cmd.Checksum, calculatedChecksum)
				continue
			}

			// 路由内存访问请求
			if err := numa.RouteMemoryAccess(
				uintptr(cmd.DstAddress),
				uint(cmd.DataSize),
				int(cmd.OpType),
				cmd.Data,
			); err != nil {
				log.Printf("Memory access failed: %v", err)
			} else {
				log.Printf("Successfully processed operation %d at 0x%x", cmd.OpType, cmd.DstAddress)
			}
		}
	}
}
