#include "advise_service.h"
#include <kj/debug.h>

AdviseService::AdviseService(CoolingService& coolingService)
    : coolingService_(coolingService) {}

kj::Promise<void> AdviseService::handleMemAdvise(HandleMemAdviseContext context) {
    auto params = context.getParams();
    uint64_t ptr = params.getPtr();
    size_t size = params.getSize();
    uint32_t advice = params.getAdvice();
    
    try {
        // 根据建议类型调用冷却服务
        switch (advice) {
            case CU_MEM_ADVISE_SET_PREFERRED_LOCATION:
                coolingService_.setPreferredLocation(ptr, size);
                break;
            case CU_MEM_ADVISE_SET_ACCESSED_BY:
                coolingService_.markAccessedBy(ptr, size);
                break;
            default:
                // 忽略不支持的advice类型
                break;
        }
        
        auto results = context.getResults();
        results.setError(Common::ErrorCode::SUCCESS);
        
        KJ_LOG(INFO, "Handled memory advice", ptr, size, advice);
    } catch (const std::exception& e) {
        KJ_LOG(ERROR, "Memory advice handling failed", e.what());
        auto results = context.getResults();
        results.setError(Common::ErrorCode::INVALID_VALUE);
    }
    
    return kj::READY_NOW;
}
