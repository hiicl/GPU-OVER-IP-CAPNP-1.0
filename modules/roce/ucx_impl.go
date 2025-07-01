package roce

import (
	"fmt"
	"log"
	"unsafe"

	"github.com/openucx/ucx"
	"github.com/hiicl/GPU-over-IP-AC922/modules/memext"
)

// 使用roce.go中定义的RoCEConfig

// UCXConnection 表示UCX连接结构
type UCXConnection struct {
	context    *ucx.UcpContext
	worker     *ucx.UcpWorker
	listener   *ucx.UcpListener
	config     *RoCEConfig
}

// NewRoCEConnection 创建新的RoCE连接实例
func NewRoCEConnection() RoCEConnection {
	return &UCXConnection{}
}

// Init 初始化UCX连接
func (c *UCXConnection) Init(config *RoCEConfig) error {
	c.config = config
	
	// 初始化UCX上下文
	params := &ucx.UcpParams{
		Features: ucx.FEATURE_RMA | ucx.FEATURE_WAKEUP,
	}
	
	var err error
	c.context, err = ucx.NewUcpContext(params)
	if err != nil {
		return fmt.Errorf("failed to create UCX context: %v", err)
	}

	// 创建worker
	workerParams := &ucx.UcpWorkerParams{
		ThreadMode: ucx.THREAD_MODE_SINGLE,
	}
	c.worker, err = c.context.NewUcpWorker(workerParams)
	if err != nil {
		return fmt.Errorf("failed to create UCX worker: %v", err)
	}

	log.Printf("[RoCE] UCX initialized, interface: %s, port: %d", 
		config.Iface, config.Port)
	return nil
}

// Send 发送数据
func (c *UCXConnection) Send(data []byte) error {
	if len(data) == 0 {
		return nil
	}

	// 注册内存区域
	memHandle, err := c.worker.RegisterMemory(data)
	if err != nil {
		return fmt.Errorf("failed to register memory: %v", err)
	}
	defer memHandle.Close()

	// 创建远程内存访问请求
	rmaParams := &ucx.UcpRequestParams{
		MemoryType: ucx.MEMORY_TYPE_HOST,
		Flags:      ucx.UCP_AM_SEND_FLAG_REPLY,
	}

	// 异步发送数据
	req, err := c.worker.SendAm(
		memHandle.Address(),
		uint64(len(data)),
		rmaParams,
	)
	if err != nil {
		return fmt.Errorf("RDMA send failed: %v", err)
	}

	// 等待发送完成
	status := req.Wait()
	if status != ucx.UCS_OK {
		return fmt.Errorf("RDMA send completion error: %s", status.String())
	}

	return nil
}

// Close 关闭连接并释放资源
func (c *UCXConnection) Close() {
	if c.listener != nil {
		c.listener.Close()
	}
	if c.worker != nil {
		c.worker.Close()
	}
	if c.context != nil {
		c.context.Close()
	}
	log.Println("[RoCE] Connection closed")
}

// RegisterWithMemext 向memext模块注册RoCE连接
func (c *UCXConnection) RegisterWithMemext() error {
	return memext.RegisterRoCEConnection(c)
}

// Receive 接收数据
func (c *UCXConnection) Receive() ([]byte, error) {
	// 创建接收缓冲区
	buf := make([]byte, 4096)
	
	// 等待接收请求
	req, err := c.worker.RecvAm(buf, nil)
	if err != nil {
		return nil, fmt.Errorf("RDMA receive failed: %v", err)
	}

	// 等待接收完成
	status := req.Wait()
	if status != ucx.UCS_OK {
		return nil, fmt.Errorf("RDMA receive error: %s", status.String())
	}

	return buf[:req.GetActualLength()], nil
}

// RegisterMemory 注册内存区域
func (c *UCXConnection) RegisterMemory(addr uintptr, size uint) error {
	_, err := c.worker.RegisterMemory(unsafe.Pointer(addr), size)
	return err
}
