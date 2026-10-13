get_filename_component(UBAHN_CXX_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

function(ubahn_add_worker_test name)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
        return()
    endif()

    add_executable(${name} "${name}.cpp")
    target_link_libraries(${name} PRIVATE Qt6::Test ubahn_worker)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ubahn_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})

    set_tests_properties(${name} PROPERTIES LABELS "worker" TIMEOUT 60)
    # Qt DLLs and the MinGW runtime, prepended so ctest does not need them on PATH (decision R13).
    set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        "PATH=path_list_prepend:${UBAHN_CXX_COMPILER_DIR}")
endfunction()

foreach(name tst_worker_smoke tst_simworker tst_simworker_thread)
    ubahn_add_worker_test(${name})
endforeach()
