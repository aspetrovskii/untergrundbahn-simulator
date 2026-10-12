qt_add_executable(ubahn_app_shell_tests tst_AppShell.cpp)
target_link_libraries(ubahn_app_shell_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_shell_tests)
add_test(NAME ubahn_app_shell_tests COMMAND ubahn_app_shell_tests)

if(NOT TARGET ubahn_worker)
    qt_add_executable(ubahn_app_mock_tests tst_MockApi.cpp)
    target_link_libraries(ubahn_app_mock_tests PRIVATE
        ubahn_app_lib
        Qt6::Test
    )
    ubahn_set_warnings(ubahn_app_mock_tests)
    add_test(NAME ubahn_app_mock_tests COMMAND ubahn_app_mock_tests)
    set_tests_properties(ubahn_app_mock_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

set_tests_properties(ubahn_app_shell_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
