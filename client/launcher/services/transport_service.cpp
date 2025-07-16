#include "transport_service.h"
#include <kj/debug.h>

TransportService::TransportService(TransportManager& transportManager)
    : transportManager_(transportManager) {}

kj::Promise<void> TransportService::executeTransfer(ExecuteTransferContext context) {
    auto params = context.getParams();
    auto src = params.getSrc();
    auto dst = params.getDst();
    size_t size = params.getSize();
    auto type = params.getTransferType();
    
    try {
        TransportManager::TransferType transportType;
        switch (type) {
            case TransferType::HOST_TO_DEVICE:
                transportType = TransportManager::HOST_TO_DEVICE;
                break;
            case TransferType::DEVICE_TO_HOST:
                transportType = TransportManager::DEVICE_TO_HOST;
                break;
            case TransferType::DEVICE_TO_DEVICE:
                transportType = TransportManager::DEVICE_TO_DEVICE;
                break;
            default:
                KJ_FAIL_REQUIRE("Invalid transfer type");
        }
        
        transportManager_.executeTransfer(src, dst, size, transportType);
        
        auto results = context.getResults();
        results.setResult(Common::Ack::SUCCESS);
        
        KJ_LOG(INFO, "Executed transfer", size, "bytes", type);
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Transfer failed", e.what());
        auto results = context.getResults();
        results.setResult(Common::Ack::FAILURE);
    }
    
    return kj::READY_NOW;
}
