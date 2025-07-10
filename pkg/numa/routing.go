// NUMA内存访问路由模块
// 负责根据内存地址所在的NUMA节点路由内存访问请求
package numa

import (
	"fmt"
	"syscall"
	"unsafe"

	"github.com/hiicl/GPU-over-IP-AC922/pkg/net"
	"github.com/hiicl/GPU-over-IP-AC922/proto/proto"
)

// 内存操作类型常量定义
const (
	MEMCPY_OPERATION = 1 // 内存复制操作
	MEMSET_OPERATION = 2 // 内存设置操作
)

// GetAddressNode 获取内存地址所属的NUMA节点
// 参数：
//   addr - 内存地址
// 返回值：
//   节点ID (失败返回-1)
func GetAddressNode(addr uintptr) int {
	var node int
	_, _, errno := syscall.Syscall6(
		syscall.SYS_MOVE_PAGES,
		0,                     // 当前进程
		1,                     // 一个页面
		uintptr(unsafe.Pointer(&addr)),
		nil,                    // 不需要页面状态
		uintptr(unsafe.Pointer(&node)),
		0,                     // 标志
	)
	if errno != 0 {
		return -1
	}
	return node
}

// RouteMemoryAccess 路由内存访问请求
// 根据目标地址所在NUMA节点决定本地或远程访问
// 参数：
//   addr - 目标内存地址
//   size - 操作数据大小
//   operation - 操作类型 (MEMCPY_OPERATION/MEMSET_OPERATION)
//   payload - 操作数据负载
// 返回值：
//   错误信息
func RouteMemoryAccess(addr uintptr, size uint, operation int, payload []byte) error {
	localNode := GetCurrentNode()
	targetNode := GetAddressNode(addr)
	
	if targetNode == localNode {
		// X-BUS本地访问
		return accessViaXBus(addr, size, operation, payload)
	} else {
		// 跨节点远程访问
		return accessViaRemote(addr, size, operation, targetNode, payload)
	}
}

// accessViaXBus 通过X-BUS进行节点内内存访问
// 使用mmap创建共享内存区域实现高效节点内内存操作
// 参数：
//   addr - 目标内存地址
//   size - 操作数据大小
//   operation - 操作类型
//   payload - 操作数据负载
// 返回值：
//   错误信息
func accessViaXBus(addr uintptr, size uint, operation int, payload []byte) error {
	dst := unsafe.Pointer(addr)
	switch operation {
	case MEMCPY_OPERATION:
		if uint(len(payload)) < size {
			return fmt.Errorf("payload size %d is less than required size %d", len(payload), size)
		}
		src := unsafe.Pointer(&payload[0])
		
		// 使用mmap创建共享内存区域
		sharedMem, err := syscall.Mmap(
			int(os.Getpid()), // 当前进程
			addr,
			int(size),
			syscall.PROT_READ|syscall.PROT_WRITE,
			syscall.MAP_SHARED|syscall.MAP_ANONYMOUS,
		)
		if err != nil {
			return fmt.Errorf("mmap failed: %v", err)
		}
		defer syscall.Munmap(sharedMem)
		
		// 使用copy函数进行高效内存复制
		copy(sharedMem, payload)
		return nil
		
	case MEMSET_OPERATION:
		if len(payload) < 1 {
			return fmt.Errorf("memset operation requires at least 1 byte value")
		}
		value := payload[0]
		
		// 使用mmap创建共享内存区域
		sharedMem, err := syscall.Mmap(
			int(os.Getpid()), // 当前进程
			addr,
			int(size),
			syscall.PROT_READ|syscall.PROT_WRITE,
			syscall.MAP_SHARED|syscall.MAP_ANONYMOUS,
		)
		if err != nil {
			return fmt.Errorf("mmap failed: %v", err)
		}
		defer syscall.Munmap(sharedMem)
		
		// 高效设置内存值
		for i := range sharedMem {
			sharedMem[i] = value
		}
		return nil
		
	default:
		return fmt.Errorf("unsupported local operation: %d", operation)
	}
}

// accessViaRemote 通过Cap'n Proto/ZeroMQ进行跨节点访问
// 将内存操作命令序列化并通过网络发送到目标NUMA节点
// 参数：
//   addr - 目标内存地址
//   size - 操作数据大小
//   operation - 操作类型
//   targetNode - 目标NUMA节点ID
//   payload - 操作数据负载
// 返回值：
//   错误信息
func accessViaRemote(addr uintptr, size uint, operation int, targetNode int, payload []byte) error {
	// 构造内存复制命令
	cmd := &proto.MemcopyCommand{
		DstAddress: uint64(addr),
		DataSize:   uint64(size),
		OpType:     uint32(operation),
		Data:       payload,
	}

	// 通过网络发送到目标NUMA节点
	if err := net.SendToRemoteNUMA(targetNode, cmd); err != nil {
		return fmt.Errorf("remote access failed: %w", err)
	}
	return nil
}
