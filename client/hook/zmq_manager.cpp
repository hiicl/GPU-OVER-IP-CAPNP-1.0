#include "zmq_manager.h"
#include <iostream>
#include "pch.h"
#include <thread> // 为std::this_thread::sleep_for添加头文件
#include <numa.h> // 添加numa头文件
#include <cstdlib> // 用于getenv
#include <zlib.h>  // 添加CRC32支持

void zmq_do_nothing_free_fn(void* data, void* hint) {}

// 绑定到指定NUMA节点
void bind_to_numa_node() {
    const char* numa_node_env = std::getenv("NUMA_NODE_ID");
    if (numa_node_env) {
        int numa_node = std::atoi(numa_node_env);
        numa_run_on_node(numa_node);
    }
}

// 获取数据面接口名称
std::string get_data_iface() {
    const char* iface = std::getenv("DATA_IFACE");
    return iface ? std::string(iface) : "";
}

// 计算CRC32校验和
uint32_t calculate_crc32(const void* data, size_t length) {
    return crc32(0, reinterpret_cast<const Bytef*>(data), length);
}

ZmqManager::ZmqManager() {}
ZmqManager::~ZmqManager() { 
    if (m_context) zmq_ctx_destroy(m_context); 
}

bool ZmqManager::Initialize() {
    m_context = zmq_ctx_new();
    return m_context != nullptr;
}

bool ZmqManager::Transfer(
    const std::string& targetIp,
    USHORT targetPort,
    void* localBuffer,
    size_t bufferSize)
{
    if (!m_context) return false;
    
    // 绑定到NUMA节点
    bind_to_numa_node();
    
    // 获取数据面接口
    std::string data_iface = get_data_iface();

    void* socket = zmq_socket(m_context, ZMQ_DGRAM);
    if (!socket) {
        std::cerr << "zmq_socket failed: " << zmq_strerror(zmq_errno()) << std::endl;
        return false;
    }

    // 绑定到指定接口（如果配置）
    if (!data_iface.empty()) {
        std::string bind_option = data_iface;
        if (zmq_setsockopt(socket, ZMQ_BINDTODEVICE, bind_option.c_str(), bind_option.size()) != 0) {
            std::cerr << "Failed to bind to interface " << data_iface 
                      << ": " << zmq_strerror(zmq_errno()) << std::endl;
        } else {
            std::cout << "Successfully bound to interface: " << data_iface << std::endl;
        }
    }

    std::string endpoint = "udp://" + targetIp + ":" + std::to_string(targetPort);
    if (zmq_connect(socket, endpoint.c_str()) != 0) {
        std::cerr << "zmq_connect failed: " << zmq_strerror(zmq_errno()) << std::endl;
        zmq_close(socket);
        return false;
    }

    // 准备带CRC校验的数据包
    size_t packetSize = bufferSize + sizeof(uint32_t);
    std::vector<uint8_t> packet(packetSize);
    
    // 复制原始数据
    memcpy(packet.data(), localBuffer, bufferSize);
    
    // 计算并附加CRC32
    uint32_t crc = calculate_crc32(localBuffer, bufferSize);
    memcpy(packet.data() + bufferSize, &crc, sizeof(uint32_t));

    int retries = 0;
    const int max_retries = 3;
    bool success = false;
    
    while (retries < max_retries && !success) {
        zmq_msg_t msg;
        if (zmq_msg_init_data(&msg, packet.data(), packetSize, zmq_do_nothing_free_fn, nullptr) != 0) {
            std::cerr << "zmq_msg_init_data failed (retry " << retries << "): " 
                      << zmq_strerror(zmq_errno()) << std::endl;
            retries++;
            continue;
        }
        
        if (zmq_msg_send(&msg, socket, 0) != -1) {
            success = true;
        } else {
            std::cerr << "zmq_msg_send failed (retry " << retries << "): " 
                      << zmq_strerror(zmq_errno()) << std::endl;
            zmq_msg_close(&msg);
            retries++;
        }
        
        if (!success && retries < max_retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    zmq_close(socket);
    return success;
}
