package memext

import (
	"fmt"
	"log"
	"os"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
	
	"github.com/hiicl/GPU-over-IP-AC922/modules/roce"
)

// memext.go 提供显存扩展功能

var (
	pool        []byte           // 共享内存池
	NumaNode    int = -1         // 绑定的NUMA节点
	ucpMemHandle *roce.UCXMemHandle // UCX内存句柄（从roce包导入）
)

// Init 初始化显存扩展模块
func Init(autoEnable bool) error {
    if !autoEnable {
        log.Println("[memext] 模块未启用")
        return nil
    }

    alloc, err := getMemExtAlloc("/mnt/memext/size")
    if err != nil {
        return fmt.Errorf("[memext] 获取共享内存池大小失败: %v", err)
    }
    if alloc <= 0 {
        return fmt.Errorf("[memext] 共享内存池大小无效: %d", alloc)
    }

    log.Printf("[memext] 映射共享内存池大小 %.2f GB", float64(alloc)/1e9)

    data, err := syscall.Mmap(
        -1, 0, int(alloc),
        syscall.PROT_READ|syscall.PROT_WRITE,
        syscall.MAP_ANON|syscall.MAP_PRIVATE|syscall.MAP_POPULATE,
    )
    if err != nil {
        return fmt.Errorf("[memext] 内存映射失败: %v", err)
    }
    pool = data

    node := getCPUNuma()
    NumaNode = node
    if node >= 0 {
        runtime.LockOSThread()
        log.Printf("[memext] 当前线程绑定 NUMA 节点: %d", node)
    }

    log.Println("[memext] 显存共享池初始化成功")
    return nil
}

// RegisterWithRoCE 注册内存到RoCE连接
func RegisterWithRoCE(conn roce.RoCEConnection) error {
	addr := uintptr(unsafe.Pointer(&pool[0]))
	size := uint(len(pool))

	// 使用新的RegisterMemory接口
	handle, err := conn.RegisterMemory(addr, size)
	if err != nil {
		return fmt.Errorf("内存注册失败: %v", err)
	}
	
	// 存储内存句柄
	ucpMemHandle = handle
	log.Printf("[memext] 已成功注册内存到RoCE连接 (句柄: %v)", handle)
	return nil
}

// UnregisterFromRoCE 从RoCE连接注销内存
func UnregisterFromRoCE(conn roce.RoCEConnection) error {
	if ucpMemHandle == nil {
		return nil // 未注册，无需注销
	}
	
	if err := conn.UnregisterMemory(); err != nil {
		return fmt.Errorf("内存注销失败: %v", err)
	}
	
	log.Println("[memext] 已从RoCE连接注销内存")
	ucpMemHandle = nil
	return nil
}

// Pool 返回内存池引用
func Pool() []byte {
	return pool
}

// Close 清理资源
func Close(conn roce.RoCEConnection) error {
	// 先注销内存
	if err := UnregisterFromRoCE(conn); err != nil {
		log.Printf("[memext] 注销内存时出错: %v", err)
	}
	
	// 解除内存映射
	if len(pool) > 0 {
		if err := syscall.Munmap(pool); err != nil {
			return fmt.Errorf("解除内存映射失败: %v", err)
		}
		pool = nil
		log.Println("[memext] 已成功解除内存映射")
	}
	
	return nil
}

// getMemExtAlloc 读取共享内存池大小
func getMemExtAlloc(path string) (int64, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return 0, err
	}
	sizeStr := strings.TrimSpace(string(data))
	size, err := strconv.ParseInt(sizeStr, 10, 64)
	if err != nil {
		return 0, err
	}
	return size, nil
}

// getCPUNuma 获取绑定的NUMA节点
func getCPUNuma() int {
	data, err := os.ReadFile("/proc/self/numa_maps")
	if err != nil {
		return -1
	}
	return parseNumaMaps(string(data))
}

// parseNumaMaps 解析numa_maps获取NUMA节点
func parseNumaMaps(s string) int {
	for _, line := range strings.Split(s, "\n") {
		if strings.Contains(line, "heap") && strings.Contains(line, "N:") {
			parts := strings.Fields(line)
			for _, f := range parts {
				if strings.HasPrefix(f, "N:") {
					n := f[2:]
					if node, err := strconv.Atoi(n); err == nil {
						return node
					}
				}
			}
		}
	}
	return -1
}
