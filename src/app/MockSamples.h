#pragma once

#include "api/Types.h"

#include <vector>

namespace ubahn::app::mock {

api::RunInfo runInfo();
std::vector<api::ViewChunkPtr> viewChunks(const api::LineConfig& line);
api::SimSummary summary();

} // namespace ubahn::app::mock
