#include "zmq_transport.h"
#include <iostream>
#include <thread>
#include <zlib.h>

void zmq_do_nothing_free_fn(void* data, void* hint) {}

ZmqTransport::ZmqTransport() {}
ZmqTransport::~ZmqTransport() { 
    if (m_context) zmq_ctx_destroy(m_context); 
}

bool ZmqTransport::Initialize() {
    m_context = zmq_ctx_new();
    return m_context != nullptr;
}

uint32_t calculate_crc32(const void* data, size_t length) {
    return crc32(0, reinterpret_cast<const Bytef*>(data), length);
}

bool ZmqTransport::Transfer(
    const std::string& targetIp,
    USHORT targetPort,
    void* localBuffer,
    size_t bufferSize)
{
    if (!m_context) return false;
    
    void* socket = zmq_socket(m_context, ZMQ_DGRAM);
    if (!socket) {
        std::cerr << "zmq_socket failed: " << zmq_strerror(zmq_errno()) << std::endl;
        return false;
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
