#pragma once
#include "hook-launcher.capnp.h"
#include <capnp/capnp.h>
#include "transport_manager.h"

class TransportService final : public GenericServices::Server {
public:
    TransportService(TransportManager& transportManager);

    // 执行传输
    ::kj::Promise<void> executeTransfer(ExecuteTransferContext context) override;

private:
    TransportManager& transportManager_;
};
