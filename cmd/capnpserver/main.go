package main

import (
	"bytes"
	"context"
	"encoding/binary"
	"flag"
	"fmt"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
	"unsafe"
	
	// CUDA驱动API
	"github.com/c3systems/cuda"

	"capnproto.org/go/capnp/v3"
	"capnproto.org/go/capnp/v3/rpc"
	"github.com/pebbe/zmq4"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"

	"github.com/hiicl/GPU-over-IP-AC922/pkg/numa"         // NUMA支持
	"github.com/hiicl/GPU-over-IP-AC922/proto"            // 协议定义
)

// CapnpServer 实现Cap'n Proto GPU服务
type CapnpServer struct {
    gpuStore  *gpu.GPUStore
    scheduler *gpu.Scheduler
    log       *logrus.Logger
    funcCache map[uint64]cuda.Function // CUDA函数缓存
    modCache  map[string]cuda.Module  // 模块缓存
    funcMeta  map[uint64]string       // 函数元数据缓存（句柄->函数名）
    paramPool *sync.Pool             // 参数缓冲区池
}

// 实现GPUService接口
func (s *CapnpServer) ListGpus(ctx context.Context, call gpu.GPUService_listGpus) error {
    res, err := call.AllocResults()
    if err != nil {
        return err
    }
    
    gpus := s.gpuStore.List()
    list := res.InitGpus(int32(len(gpus)))
    for i, g := range gpus {
        list.Set(i, g)
    }
    return nil
}

func (s *CapnpServer) GetGpuStatus(ctx context.Context, call gpu.GPUService_getGpuStatus) error {
    req, err := call.Params()
    if err != nil {
        return err
    }
    
    status := s.gpuStore.GetStatus(req.GetUuid())
    res, err := call.AllocResults()
    if err != nil {
        return err
    }
    
    res.SetStatus(status)
    return nil
}

func (s *CapnpServer) AcquireGpu(ctx context.Context, call gpu.GPUService_acquireGpu) error {
    req, err := call.Params()
    if err != nil {
        return err
    }
    
    ack := s.gpuStore.Acquire(req.GetUuid())
    res, err := call.AllocResults()
    if err != nil {
        return err
    }
    
    res.SetAck(ack)
    return nil
}

func (s *CapnpServer) ReleaseGpu(ctx context.Context, call gpu.GPUService_releaseGpu) error {
    req, err := call.Params()
    if err != nil {
        return err
    }
    
    ack := s.gpuStore.Release(req.GetUuid())
    res, err := call.AllocResults()
    if err != nil {
        return err
    }
    
    res.SetAck(ack)
    return nil
}

func (s *CapnpServer) RunCommand(ctx context.Context, call gpu.GPUService_runCommand) error {
    req, err := call.Params()
    if err != nil {
        return err
    }
    
    resp := s.scheduler.Run(req.GetCmd(), req.GetStreamHandle())
    res, err := call.AllocResults()
    if err != nil {
        return err
    }
    
    res.SetResponse(resp)
    return nil
}

func NewCapnpServer() *CapnpServer {
	log := util.InitLogger(viper.GetString("log_level"))
	
	// 初始化NUMA资源
	nodes, err := numa.DiscoverNodes()
	if err != nil {
		log.Fatalf("NUMA discovery failed: %v", err)
	}
	log.Infof("Discovered %d NUMA nodes", len(nodes))
	
	// NUMA绑定由容器运行时处理
	// 注意: 资源管理器节点隔离已通过容器级NUMA绑定实现
	
	gpuStore := gpu.NewGPUStore()
	// 容器级NUMA绑定已确保只包含本节点资源
	scheduler := gpu.NewScheduler(gpuStore)
	
	// 初始化CUDA驱动
	if err := cuda.Init(); err != nil {
		log.Errorf("CUDA初始化失败: %v", err)
	} else {
		log.Info("CUDA驱动初始化成功")
	}
	
	// 初始化参数缓冲区池
	paramPool := &sync.Pool{
		New: func() interface{} {
			return make([]byte, 0, 1024) // 初始容量1KB
		},
	}
	
	return &CapnpServer{
		gpuStore:  gpuStore,
		scheduler: scheduler,
		log:       log,
		funcCache: make(map[uint64]cuda.Function),
		modCache:  make(map[string]cuda.Module),
		funcMeta:  make(map[uint64]string),
		paramPool: paramPool,
	}
}

func (s *CapnpServer) StartZMQReceiver(port string) {
	ctx, err := zmq4.NewContext()
	if err != nil {
		s.log.Errorf("Failed to create ZMQ context: %v", err)
		return
	}
	defer ctx.Term()

	socket, err := ctx.NewSocket(zmq4.DGRAM)
	if err != nil {
		s.log.Errorf("Failed to create ZMQ socket: %v", err)
		return
	}
	defer socket.Close()

	endpoint := fmt.Sprintf("udp://*:%s", port)
	if err := socket.Bind(endpoint); err != nil {
		s.log.Errorf("Failed to bind to %s: %v", endpoint, err)
		return
	}
	s.log.Infof("ZMQ data receiver started on port %s", port)
	if err := socket.SetRcvhwm(1000); err != nil {
		s.log.Errorf("Failed to set receive HWM: %v", err)
	}

	workQueue := make(chan []byte, 1024)
	defer close(workQueue)

	var wg sync.WaitGroup
	for i := 0; i < 4; i++ {
		wg.Add(1)
		go func(id int) {
			defer wg.Done()
			for msg := range workQueue {
				s.processMessage(msg, id)
			}
		}(i)
	}

	for {
		msg, err := socket.RecvBytes(0)
		if err != nil {
			s.log.Errorf("Error receiving message: %v", err)
			continue
		}
		select {
		case workQueue <- msg:
		default:
			s.log.Warn("Work queue full, dropping message")
		}
	}
}

func (s *CapnpServer) processMessage(msg []byte, workerID int) {
	const headerSize = 32
	const maxRetries = 3

	if len(msg) < headerSize {
		s.log.Errorf("[worker-%d] Message too short (%d bytes)", workerID, len(msg))
		return
	}

	header := msg[:headerSize]
	metadata := struct {
		Operation  uint8
		DstDevice  uint64
		DataSize   uint32
		Reserved   [19]byte
	}{}
	if err := binary.Read(bytes.NewReader(header), binary.LittleEndian, &metadata); err != nil {
		s.log.Errorf("[worker-%d] Failed to parse header: %v", workerID, err)
		return
	}

	expectedSize := headerSize + int(metadata.DataSize)
	if len(msg) < expectedSize {
		s.log.Errorf("[worker-%d] Data size mismatch: expected %d, got %d", workerID, expectedSize, len(msg))
		return
	}

	payload := msg[headerSize : headerSize+int(metadata.DataSize)]
	switch metadata.Operation {
	case 1: // cudaMemcpy
		for retry := 0; retry < maxRetries; retry++ {
			err := gpu.SafeMemcpy(unsafe.Pointer(uintptr(metadata.DstDevice)), unsafe.Pointer(&payload[0]), metadata.DataSize)
			if err == nil {
				s.log.Debugf("[worker-%d] Copied %d bytes to 0x%x", workerID, metadata.DataSize, metadata.DstDevice)
				return
			}
			s.log.Warnf("[worker-%d] cudaMemcpy failed (attempt %d/%d): %v", workerID, retry+1, maxRetries, err)
			time.Sleep(time.Duration(retry*100) * time.Millisecond)
		}
		s.log.Errorf("[worker-%d] Failed to copy %d bytes after %d retries", workerID, metadata.DataSize, maxRetries)
	case 2:
		s.log.Warnf("[worker-%d] cudaMemset not implemented yet", workerID)
	default:
		s.log.Errorf("[worker-%d] Unsupported operation: %d", workerID, metadata.Operation)
	}
}

// 实现CudaService接口方法
func (s *CapnpServer) cudaMemAlloc(ctx context.Context, size uint64) (uint64, error) {
	ptr, err := cuda.Malloc(size)
	if err != nil {
		s.log.Errorf("内存分配失败: %v", err)
		return 0, err
	}
	return uint64(ptr), nil
}

func (s *CapnpServer) cudaKernelLaunch(ctx context.Context, req proto.KernelLaunch) error {
	// 记录详细启动参数
	s.log.Infof("内核启动请求: %s", req.Name())
	s.log.Infof("网格尺寸: (%d, %d, %d)", req.GridX(), req.GridY(), req.GridZ())
	s.log.Infof("块尺寸: (%d, %d, %d)", req.BlockX(), req.BlockY(), req.BlockZ())
	s.log.Infof("共享内存: %d bytes", req.SharedMem())
	s.log.Infof("参数数量: %d", req.Params().Len())
	s.log.Infof("参数总大小: %d bytes", req.ParamBytes())
	s.log.Infof("函数句柄: 0x%x", req.FuncHandle())
	s.log.Infof("启动标志: 0x%x", req.LaunchFlags())

	// 1. 验证参数
	if req.Params().Len() == 0 {
		return fmt.Errorf("内核参数不能为空")
	}
	
	// 2. 准备参数缓冲区
	paramBuffer, err := s.prepareParamBuffer(req)
	if err != nil {
		return fmt.Errorf("准备参数缓冲区失败: %w", err)
	}
	defer cuda.Free(paramBuffer)
	
	// 3. 获取CUDA函数
	cudaFunc, err := s.getCudaFunction(req)
	if err != nil {
		return fmt.Errorf("获取CUDA函数失败: %w", err)
	}
	
	// 4. 执行内核
	gridDim := [3]uint32{req.GridX(), req.GridY(), req.GridZ()}
	blockDim := [3]uint32{req.BlockX(), req.BlockY(), req.BlockZ()}
	
	err = cuda.LaunchKernelEx(
		cudaFunc,
		gridDim,
		blockDim,
		paramBuffer,
		req.SharedMem(),
		nil, // 使用默认流
	)
	
	if err != nil {
		return fmt.Errorf("内核启动失败: %w", err)
	}
	
	s.log.Infof("内核 %s 启动成功", req.Name())
	return nil
}

// 准备参数缓冲区（使用内存池优化）
func (s *CapnpServer) prepareParamBuffer(req proto.KernelLaunch) (cuda.DevicePtr, error) {
	totalSize := req.ParamBytes()
	
	// 从池中获取或创建缓冲区
	buf := s.paramPool.Get().([]byte)
	defer s.paramPool.Put(buf[:0]) // 使用后重置并放回池中
	
	// 确保缓冲区足够大
	if cap(buf) < int(totalSize) {
		buf = make([]byte, totalSize)
	} else {
		buf = buf[:totalSize]
	}
	
	// 合并参数到连续缓冲区
	offset := 0
	for i := 0; i < req.Params().Len(); i++ {
		param := req.Params().At(i)
		copy(buf[offset:], param.Value())
		offset += len(param.Value())
	}
	
	// 分配设备内存
	paramBuf, err := cuda.Malloc(totalSize)
	if err != nil {
		return 0, err
	}
	
	// 单次复制整个缓冲区
	if err := cuda.Memcpy(
		paramBuf,
		cuda.HostPtr(unsafe.Pointer(&buf[0])),
		totalSize,
		cuda.MemcpyHostToDevice,
	); err != nil {
		cuda.Free(paramBuf)
		return 0, err
	}
	
	return paramBuf, nil
}

// 获取CUDA函数（优化版）
func (s *CapnpServer) getCudaFunction(req proto.KernelLaunch) (cuda.Function, error) {
	handle := uint64(req.FuncHandle())
	
	// 检查缓存
	if f, ok := s.funcCache[handle]; ok {
		return f, nil
	}
	
	// 获取函数元数据
	funcName, ok := s.funcMeta[handle]
	if !ok {
		funcName = req.Name() // 回退到请求中的名称
		s.log.Warnf("函数句柄 %d 缺少元数据，使用请求名称: %s", handle, funcName)
	}
	
	// 模块名称（简化处理）
	modName := "default"
	
	// 检查模块缓存
	module, ok := s.modCache[modName]
	if !ok {
		// 加载模块
		var err error
		module, err = cuda.ModuleLoad(modName + ".cubin")
		if err != nil {
			return 0, fmt.Errorf("模块加载失败: %w", err)
		}
		s.modCache[modName] = module
		s.log.Infof("已加载模块: %s", modName)
	}
	
	// 获取函数
	cudaFunc, err := module.GetFunction(funcName)
	if err != nil {
		return 0, fmt.Errorf("函数获取失败: %w", err)
	}
	
	// 缓存函数
	s.funcCache[handle] = cudaFunc
	s.log.Infof("已缓存函数: %s (句柄: %d)", funcName, handle)
	return cudaFunc, nil
}

// Cap'n Proto 接口方法

func main() {
	dataPort := flag.String("dataPort", "5555", "ZMQ data port")
	flag.Parse()

	viper.SetConfigName("config")
	viper.AddConfigPath(".")
	if err := viper.ReadInConfig(); err != nil {
		panic(fmt.Errorf("fatal error config file: %w", err))
	}

	controlIface := os.Getenv("CONTROL_IFACE")
	if controlIface == "" {
		panic("必须设置CONTROL_IFACE环境变量")
	}

	if _, err := net.InterfaceByName(controlIface); err != nil {
		panic(fmt.Sprintf("控制面网卡%s不存在: %v", controlIface, err))
	}

	server := NewCapnpServer()
	go server.StartZMQReceiver(*dataPort)

	addr := viper.GetString("server.address")
	listener, err := net.Listen("tcp", addr)
	if err != nil {
		panic(fmt.Errorf("failed to listen on %s: %w", addr, err))
	}
	defer listener.Close()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)

	server.log.Infof("Cap'n Proto server listening on %s", addr)
	var wg sync.WaitGroup

	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				server.log.Errorf("accept error: %v", err)
				continue
			}

			wg.Add(1)
			go func() {
				defer wg.Done()
				transport := rpc.NewStreamTransport(conn)
				defer transport.Close()

				main := gpu.GPUService_ServerToClient(server)
				conn := rpc.NewConn(transport, &rpc.Options{BootstrapClient: capnp.Client(main)})
				defer conn.Close()

				<-conn.Done()
			}()
		}
	}()

	<-sig
	server.log.Infof("Shutting down server")
	listener.Close()
	wg.Wait()
}
