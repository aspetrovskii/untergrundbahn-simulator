#include "api/SimController.h"

namespace ubahn::api {

void registerMetaTypes() {
    qRegisterMetaType<SimParams>();
    qRegisterMetaType<ScheduleConfig>();
    qRegisterMetaType<SimTime>("ubahn::api::SimTime");
    qRegisterMetaType<RunInfo>();
    qRegisterMetaType<ViewChunkPtr>();
    qRegisterMetaType<SimSummary>();
    qRegisterMetaType<RuntimeError>();
}

} // namespace ubahn::api
