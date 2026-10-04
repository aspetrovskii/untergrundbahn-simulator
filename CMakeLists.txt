cmake_minimum_required(VERSION 3.21)

project(untergrundbahn-simulator
    VERSION 0.1.0
    DESCRIPTION "Metro line traffic control simulator"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)
qt_standard_project_setup()

enable_testing()

function(ubahn_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# Each side owns the CMakeLists.txt in its own directories; missing ones are skipped,
# so the backend and frontend branches configure without each other's code.
foreach(dir
        src/api
        src/core
        src/worker
        src/app
        tests/core
        tests/worker
        tests/app)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
        add_subdirectory(${dir})
    endif()
endforeach()
