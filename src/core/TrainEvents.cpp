#include "core/TrainEvents.h"

namespace ubahn::core {

void handleTrain(EngineState&, int, api::SimTime, DirtySet&) {
    // TODO(BE-11): ALGORITHM 8.
}

void applyReschedule(EngineState&, int, const NextEvent&, api::SimTime) {
    // TODO(BE-11): ALGORITHM 7.4.
}

int32_t drawDelay(EngineState&, int, api::SimTime) {
    // TODO(BE-11): ALGORITHM 9. Honour delayOverride when it is set.
    return 0;
}

void trackPushBack(EngineState&, api::Dir, int) {
    // TODO(BE-11): ALGORITHM 5.5.
}

void trackRemove(EngineState&, api::Dir, int) {
    // TODO(BE-11): ALGORITHM 5.5.
}

} // namespace ubahn::core
