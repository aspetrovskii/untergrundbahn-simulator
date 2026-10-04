qt_add_library(ubahn_api STATIC
    Version.h
    Types.h
    Functions.h
    SimController.h
    SimController.cpp
)

target_include_directories(ubahn_api PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(ubahn_api PUBLIC Qt6::Core)
ubahn_set_warnings(ubahn_api)
