package main

import (
	"context"
	"flag"
	"fmt"
	"net"
	"os"
	"os/signal"
	"sync"
	"syscall"

	"capnproto.org/go/capnp/v3"
	"capnproto.org/go/capnp/v3/rpc"
	"github.com/pebbe/zmq4"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"

	"github.com/hiicl/GPU-over-IP-AC922/modules/gpumanager/core"
	"github.com/hiicl/GPU-over-IP-AC922/modules/gpumanager/query"
	"github.com/hiicl/GPU-over-IP-AC922/modules/roce"
	"github.com/hiicl/GPU-over-IP-AC922/modules/gpumanager/util"
)

// CapnpServer 实现Cap'n Proto GPU服务
type CapnpServer struct {
	gpuStore  *gpu.GPUStore
	roceMgr   *roce.RoCEManager
	scheduler *gpu.Scheduler
	log       *logrus.Logger
}

func NewCapnpServer() *CapnpServer {
	log := util.InitLogger(viper.GetString("log_level"))
	
	gpuStore := gpu.NewGPUStore()
	scheduler := gpu.NewScheduler(gpuStore)
	
	return &CapnpServer{
		gpuStore:  gpuStore,
		roceMgr:   roce.NewRoCEManager(),
		scheduler: scheduler,
		log:       log,
	}
}

// 启动ZMQ数据接收器
func (s *CapnpServer) StartZMQReceiver(port string) {
	context, err := zmq4.NewContext()
	if err != nil {
		s.log.Errorf("Failed to create ZMQ context: %v", err)
		return
	}
	defer context.Term()

	socket, err := context.NewSocket(zmq4.DGRAM)
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

	// 主接收循环
	for {
		msg, err := socket.RecvBytes(0)
		if err != nil {
			s.log.Errorf("Error receiving message: %v", err)
			continue
		}

		// 这里添加CUDA内存拷贝逻辑
		// 伪代码: cudaMemcpy(dstDevice, msg, size, cudaMemcpyHostToDevice)
		s.log.Debugf("Received %d bytes of data via ZMQ", len(msg))
	}
}

// 实现Cap'n Proto接口
func (s *CapnpServer) ListGPUs(ctx context.Context, call gpu.GPUService_listGPUs) error {
	gpuList, err := s.gpuStore.ListGPUs()
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	// 创建GPU信息列表
	gpuInfos, err := resp.NewGpus(int32(len(gpuList)))
	if err != nil {
		return err
	}

	for i, info := range gpuList {
		gpuInfo := gpuInfos.At(i)
		gpuInfo.SetUuid(info.UUID)
		gpuInfo.SetName(info.Name)
		gpuInfo.SetTotalMemory(info.TotalMemory)
		gpuInfo.SetFreeMemory(info.FreeMemory)
	}

	return nil
}

func (s *CapnpServer) GetGPUStatus(ctx context.Context, call gpu.GPUService_getGPUStatus) error {
	req, err := call.Params.Request()
	if err != nil {
		return err
	}

	uuid, err := req.Uuid()
	if err != nil {
		return err
	}

	status, err := s.gpuStore.GetStatus(uuid)
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	statusMsg, err := resp.NewStatus()
	if err != nil {
		return err
	}

	statusMsg.SetUuid(status.UUID)
	statusMsg.SetStatus(string(status.Status))
	statusMsg.SetOwner(status.Owner)
	return nil
}

func (s *CapnpServer) AcquireGPU(ctx context.Context, call gpu.GPUService_acquireGPU) error {
	req, err := call.Params.Request()
	if err != nil {
		return err
	}

	uuid, err := req.Uuid()
	if err != nil {
		return err
	}

	err = s.scheduler.Acquire(uuid, "default-client")
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	ack, err := resp.NewAck()
	if err != nil {
		return err
	}

	ack.SetSuccess(true)
	ack.SetMessage("GPU acquired successfully")
	return nil
}

func (s *CapnpServer) ReleaseGPU(ctx context.Context, call gpu.GPUService_releaseGPU) error {
	req, err := call.Params.Request()
	if err != nil {
		return err
	}

	uuid, err := req.Uuid()
	if err != nil {
		return err
	}

	err = s.scheduler.Release(uuid)
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	ack, err := resp.NewAck()
	if err != nil {
		return err
	}

	ack.SetSuccess(true)
	ack.SetMessage("GPU released successfully")
	return nil
}

// CUDA操作实现
func (s *CapnpServer) CUDAInit(ctx context.Context, call gpu.GPUService_cudaInit) error {
	// 初始化CUDA环境
	err := s.roceMgr.Init()
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	ack, err := resp.NewAck()
	if err != nil {
		return err
	}

	ack.SetSuccess(true)
	return nil
}

func (s *CapnpServer) CUDAMemAlloc(ctx context.Context, call gpu.GPUService_cudaMemAlloc) error {
	info, err := call.Params.Info()
	if err != nil {
		return err
	}

	size := info.Size()
	addr, err := s.roceMgr.Allocate(size)
	if err != nil {
		return err
	}

	// 创建Cap'n Proto响应
	resp, err := call.AllocResults()
	if err != nil {
		return err
	}

	result, err := resp.NewResult()
	if err != nil {
		return err
	}

	result.SetAddr(addr)
	result.SetSize(size)
	return nil
}

func main() {
	// 解析命令行参数
	dataPort := flag.String("dataPort", "5555", "ZMQ data port")
	flag.Parse()

	// 初始化配置
	viper.SetConfigName("config")
	viper.AddConfigPath(".")
	if err := viper.ReadInConfig(); err != nil {
		panic(fmt.Errorf("fatal error config file: %w", err))
	}

	// 从环境变量获取网卡配置
	controlIface := os.Getenv("CONTROL_IFACE")
	dataIface := os.Getenv("ROCE_IFACE")
	if controlIface == "" || dataIface == "" {
		panic("必须设置CONTROL_IFACE和ROCE_IFACE环境变量")
	}

	// 验证网卡存在
	if _, err := net.InterfaceByName(controlIface); err != nil {
		panic(fmt.Sprintf("控制面网卡%s不存在: %v", controlIface, err))
	}
	if _, err := net.InterfaceByName(dataIface); err != nil {
		panic(fmt.Sprintf("数据面网卡%s不存在: %v", dataIface, err))
	}

	// 创建Cap'n Proto服务器
	server := NewCapnpServer()
	
	// 启动ZMQ数据接收器
	go server.StartZMQReceiver(*dataPort)
	
	// 启动Cap'n Proto RPC服务器
	addr := viper.GetString("server.address")
	listener, err := net.Listen("tcp", addr)
	if err != nil {
		panic(fmt.Errorf("failed to listen on %s: %w", addr, err))
	}
	defer listener.Close()
	
	server.log.Infof("Cap'n Proto server listening on %s", addr)

	// 主循环
	var wg sync.WaitGroup
	for {
		conn, err := listener.Accept()
		if err != nil {
			server.log.Errorf("accept error: %v", err)
			continue
		}

		wg.Add(1)
		go func() {
			defer wg.Done()
			
			// 创建RPC连接
			transport := rpc.NewStreamTransport(conn)
			defer transport.Close()
			
			// 提供GPU服务
			main := gpu.GPUService_ServerToClient(server)
			conn := rpc.NewConn(transport, &rpc.Options{
				BootstrapClient: capnp.Client(main),
			})
			defer conn.Close()
			
			// 等待连接关闭
			<-conn.Done()
		}()
	}
	wg.Wait()
}
