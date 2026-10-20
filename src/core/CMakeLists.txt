# Static engine library. No Qt and no ubahn_api: the api headers it includes are header-only.
add_library(ubahn_core STATIC
    Time.h
    Check.h
    Rng.h
    Rng.cpp
    IndexedMinHeap.h
    IndexedMinHeap.cpp
    Line.h
    Line.cpp
    Schedule.h
    Schedule.cpp
    Trips.h
    Trips.cpp
    Validation.cpp
    Presets.cpp
    EngineState.h
    Engine.h
    Engine.cpp
    Regulator.h
    Regulator.cpp
    TrainEvents.h
    TrainEvents.cpp
    Dispatcher.h
    Dispatcher.cpp
    Calendar.h
    Calendar.cpp
    Publisher.h
    Publisher.cpp
    Stats.h
    Stats.cpp
    Invariants.h
    Invariants.cpp
)

target_include_directories(ubahn_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(ubahn_core PUBLIC $<$<CONFIG:Debug>:UBAHN_CHECK_INVARIANTS>)
set_target_properties(ubahn_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
ubahn_set_warnings(ubahn_core)
