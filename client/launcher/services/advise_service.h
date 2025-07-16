#pragma once
#include "hook-launcher.capnp.h"
#include <capnp/capnp.h>
#include "cooling_service.h"

class AdviseService final : public GenericServices::Server {
public:
    AdviseService(CoolingService& coolingService);

    // 处理内存建议
    ::kj::Promise<void> handleMemAdvise(HandleMemAdviseContext context) override;

private:
    CoolingService& coolingService_;
};
