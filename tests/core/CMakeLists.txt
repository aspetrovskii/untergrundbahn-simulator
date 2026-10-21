get_filename_component(UBAHN_CXX_COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

function(ubahn_add_core_test name)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp")
        return()
    endif()

    add_executable(${name} "${name}.cpp")
    target_link_libraries(${name} PRIVATE Qt6::Test ubahn_core)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ubahn_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})

    set(labels core)
    set(timeout 60)
    if(name MATCHES "^tst_acceptance_")
        list(APPEND labels acceptance)
        set(timeout 600)
    endif()
    if(name STREQUAL "tst_acceptance_perf")
        list(APPEND labels perf)
    endif()

    set_tests_properties(${name} PROPERTIES LABELS "${labels}" TIMEOUT "${timeout}")
    # Qt DLLs and the MinGW runtime, prepended so ctest does not need them on PATH (decision R13).
    # The compiler directory is prepended last, so it stays in front of Qt.
    set_property(TEST ${name} PROPERTY ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        "PATH=path_list_prepend:${UBAHN_CXX_COMPILER_DIR}")
endfunction()

set(UBAHN_CORE_TESTS
    tst_smoke
    tst_heap
    tst_rng
    tst_line
    tst_schedule
    tst_trips
    tst_validation
    tst_engine_contract
    tst_presets
    tst_engine_loop
    tst_regulator
    tst_train_events
    tst_dispatcher
    tst_calendar
    tst_publisher
    tst_performance_regressions
    tst_stats
    tst_invariants
    tst_acceptance_nodelay
    tst_acceptance_random
    tst_acceptance_recovery
    tst_acceptance_perf
    tst_acceptance_presets
    tst_acceptance_live
)

foreach(name IN LISTS UBAHN_CORE_TESTS)
    ubahn_add_core_test(${name})
endforeach()
