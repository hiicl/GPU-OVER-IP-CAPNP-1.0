package numa

import (
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"unsafe"
)

const (
	ocxlDevDir = "/dev/ocxl"
)

// OpenCapiDevice represents an OpenCAPI device
type OpenCapiDevice struct {
	Name     string // Device name (e.g., IBM,oc-snap.0000:00:00.1.0)
	Device   string // Device path (e.g., /dev/ocxl/IBM,oc-snap.0000:00:00.1.0)
	AFUName  string // AFU name
	NodeID   int    // Associated NUMA node
	Memory   uint64 // Device memory size (bytes)
}

// DiscoverOpenCapiDevices discovers OpenCAPI devices in the system
func DiscoverOpenCapiDevices() ([]OpenCapiDevice, error) {
	devices := []OpenCapiDevice{}

	// Check if /dev/ocxl directory exists
	if _, err := os.Stat(ocxlDevDir); os.IsNotExist(err) {
		return nil, fmt.Errorf("OpenCAPI not supported: %v", err)
	}

	// Iterate through /dev/ocxl directory
	entries, err := os.ReadDir(ocxlDevDir)
	if err != nil {
		return nil, fmt.Errorf("failed to read %s: %w", ocxlDevDir, err)
	}

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}

		devPath := filepath.Join(ocxlDevDir, entry.Name())
		dev := OpenCapiDevice{
			Name:   entry.Name(),
			Device: devPath,
		}

		// Get device info
		if err := dev.getDeviceInfo(); err != nil {
			continue // Skip problematic devices
		}
		devices = append(devices, dev)
	}

	return devices, nil
}

// getDeviceInfo retrieves device details
func (d *OpenCapiDevice) getDeviceInfo() error {
	// Open device
	fd, err := syscall.Open(d.Device, syscall.O_RDWR, 0)
	if err != nil {
		return fmt.Errorf("failed to open device %s: %w", d.Device, err)
	}
	defer syscall.Close(fd)

	// Get AFU name
	var nameBuf [64]byte
	if _, _, errno := syscall.Syscall(
		syscall.SYS_IOCTL,
		uintptr(fd),
		uintptr(0x4F43), // OCXL_IOCTL_GET_AFU_NAME
		uintptr(unsafe.Pointer(&nameBuf[0])),
	); errno != 0 {
		return fmt.Errorf("ioctl OCXL_IOCTL_GET_AFU_NAME failed: %v", errno)
	}
	d.AFUName = strings.Trim(string(nameBuf[:]), "\x00")

	// Get NUMA node and memory size from sysfs
	sysPath := filepath.Join("/sys/bus/ocxl/devices", d.Name)
	nodePath := filepath.Join(sysPath, "numa_node")
	if nodeData, err := os.ReadFile(nodePath); err == nil {
		nodeStr := strings.TrimSpace(string(nodeData))
		if nodeID, err := strconv.Atoi(nodeStr); err == nil {
			d.NodeID = nodeID
		}
	}

	memPath := filepath.Join(sysPath, "global_mmio_size")
	if memData, err := os.ReadFile(memPath); err == nil {
		memStr := strings.TrimSpace(string(memData))
		if memSize, err := strconv.ParseUint(memStr, 10, 64); err == nil {
			d.Memory = memSize
		}
	}

	return nil
}

// MapMemory maps device memory to process address space
func (d *OpenCapiDevice) MapMemory(offset, size uint64) ([]byte, error) {
	fd, err := syscall.Open(d.Device, syscall.O_RDWR, 0)
	if err != nil {
		return nil, fmt.Errorf("failed to open device %s: %w", d.Device, err)
	}
	defer syscall.Close(fd)

	// Call mmap
	addr, _, errno := syscall.Syscall6(
		syscall.SYS_MMAP,
		0, // Let system choose address
		uintptr(size),
		syscall.PROT_READ|syscall.PROT_WRITE,
		syscall.MAP_SHARED,
		uintptr(fd),
		uintptr(offset),
	)
	if errno != 0 {
		return nil, fmt.Errorf("mmap failed: %v", errno)
	}

	// Convert to byte slice
	data := (*[1<<30]byte)(unsafe.Pointer(addr))[:size:size]
	return data, nil
}

// UnmapMemory unmaps memory
func (d *OpenCapiDevice) UnmapMemory(data []byte) error {
	if len(data) == 0 {
		return nil
	}
	addr := uintptr(unsafe.Pointer(&data[0]))
	size := uintptr(len(data))
	_, _, errno := syscall.Syscall(syscall.SYS_MUNMAP, addr, size, 0)
	if errno != 0 {
		return fmt.Errorf("munmap failed: %v", errno)
	}
	return nil
}
