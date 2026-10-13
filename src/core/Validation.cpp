#include "api/Functions.h"

namespace ubahn::api {

std::vector<ValidationIssue> validate(const SimParams&) {
    // TODO(BE-06): SPEC 9.1, 9.4
    return {};
}

std::vector<ValidationIssue> validateSchedule(const LineConfig&, const ScheduleConfig&) {
    // TODO(BE-06)
    return {};
}

} // namespace ubahn::api
