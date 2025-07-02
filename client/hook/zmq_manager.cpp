#include "zmq_manager.h"
#include <iostream>

void zmq_do_nothing_free_fn(void* data, void* hint) {}

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

    zmq_msg_t msg;
    if (zmq_msg_init_data(&msg, localBuffer, bufferSize, zmq_do_nothing_free_fn, nullptr) != 0) {
        std::cerr << "zmq_msg_init_data failed: " << zmq_strerror(zmq_errno()) << std::endl;
        zmq_close(socket);
        return false;
    }
    
    if (zmq_msg_send(&msg, socket, 0) == -1) {
        std::cerr << "zmq_msg_send failed: " << zmq_strerror(zmq_errno()) << std::endl;
        zmq_close(socket);
        return false;
    }
    
    zmq_close(socket);
    return true;
}
