qt_add_library(ubahn_worker STATIC
    SimWorker.h
    SimWorker.cpp
    CreateSimController.cpp
)

target_link_libraries(ubahn_worker PUBLIC ubahn_api ubahn_core Qt6::Core)
ubahn_set_warnings(ubahn_worker)
