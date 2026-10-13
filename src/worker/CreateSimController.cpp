#include "worker/SimWorker.h"

#include <memory>

namespace ubahn::api {

std::unique_ptr<SimController> createSimController() {
    return std::make_unique<ubahn::worker::SimWorker>();
}

} // namespace ubahn::api
