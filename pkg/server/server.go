package server

import (
	"fmt"
	"sync"

	"github.com/hiicl/GPU-over-IP-AC922/pkg/driver/nvidia"
	"github.com/hiicl/GPU-over-IP-AC922/pkg/driver/numa"
)

// GPUServer 实现GPU服务核心逻辑
type GPUServer struct {
	nvidiaDriver *nvidia.Driver
	numaDriver   *numa.Driver
	mu           sync.Mutex
}

func New() *GPUServer {
	return &GPUServer{
		nvidiaDriver: nvidia.NewDriver(),
		numaDriver:   numa.NewDriver(),
	}
}

// AcquireGPU 申请GPU资源
func (s *GPUServer) AcquireGPU(uuid string, numaNode int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// 绑定NUMA节点
	if err := s.numaDriver.AcquireNUMANode(numaNode); err != nil {
		return fmt.Errorf("NUMA绑定失败: %v", err)
	}

	// 申请GPU
	if err := s.nvidiaDriver.AcquireGPU(uuid); err != nil {
		return fmt.Errorf("GPU申请失败: %v", err)
	}

	return nil
}

// ReleaseGPU 释放GPU资源
func (s *GPUServer) ReleaseGPU(uuid string, numaNode int) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	// 释放GPU
	if err := s.nvidiaDriver.ReleaseGPU(uuid); err != nil {
		return fmt.Errorf("GPU释放失败: %v", err)
	}

	// 释放NUMA节点
	if err := s.numaDriver.ReleaseNUMANode(numaNode); err != nil {
		return fmt.Errorf("NUMA释放失败: %v", err)
	}

	return nil
}

// Monitor 监控资源状态
func (s *GPUServer) Monitor() (map[string]interface{}, error) {
	gpuStatus, err := s.nvidiaDriver.MonitorGPUs()
	if err != nil {
		return nil, err
	}

	numaStatus, err := s.numaDriver.MonitorNUMANodes()
	if err != nil {
		return nil, err
	}

	return map[string]interface{}{
		"gpus": gpuStatus,
		"numa": numaStatus,
	}, nil
}
