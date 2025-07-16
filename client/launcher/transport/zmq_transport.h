#pragma once

#include <string>
#include <memory>
#include <zmq.h>

class ZmqTransport {
public:
    ZmqTransport();
    ~ZmqTransport();

    bool Initialize();
    bool Transfer(
        const std::string& targetIp,
        USHORT targetPort,
        void* localBuffer,
        size_t bufferSize
    );

private:
    void* m_context = nullptr;
};
