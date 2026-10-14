qt_add_executable(ubahn_app_shell_tests tst_AppShell.cpp)
target_link_libraries(ubahn_app_shell_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_shell_tests)
add_test(NAME ubahn_app_shell_tests COMMAND ubahn_app_shell_tests)

qt_add_executable(ubahn_app_playback_tests tst_PlaybackController.cpp)
target_link_libraries(ubahn_app_playback_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
)
ubahn_set_warnings(ubahn_app_playback_tests)
add_test(NAME ubahn_app_playback_tests COMMAND ubahn_app_playback_tests)

qt_add_executable(ubahn_app_line_view_tests tst_LineView.cpp)
target_link_libraries(ubahn_app_line_view_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_line_view_tests)
add_test(NAME ubahn_app_line_view_tests COMMAND ubahn_app_line_view_tests)

qt_add_executable(ubahn_app_board_tests tst_BoardWidget.cpp)
target_link_libraries(ubahn_app_board_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_board_tests)
add_test(NAME ubahn_app_board_tests COMMAND ubahn_app_board_tests)

qt_add_executable(ubahn_app_playback_panel_tests tst_PlaybackPanel.cpp)
target_link_libraries(ubahn_app_playback_panel_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_playback_panel_tests)
add_test(NAME ubahn_app_playback_panel_tests COMMAND ubahn_app_playback_panel_tests)

qt_add_executable(ubahn_app_setup_editor_tests tst_SetupEditors.cpp)
target_link_libraries(ubahn_app_setup_editor_tests PRIVATE
    ubahn_app_lib
    Qt6::Test
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_setup_editor_tests)
add_test(NAME ubahn_app_setup_editor_tests COMMAND ubahn_app_setup_editor_tests)

if(NOT TARGET ubahn_worker)
    qt_add_executable(ubahn_app_session_tests tst_AppSession.cpp)
    target_link_libraries(ubahn_app_session_tests PRIVATE
        ubahn_app_lib
        Qt6::Test
    )
    ubahn_set_warnings(ubahn_app_session_tests)
    add_test(NAME ubahn_app_session_tests COMMAND ubahn_app_session_tests)

    qt_add_executable(ubahn_app_mock_tests tst_MockApi.cpp)
    target_link_libraries(ubahn_app_mock_tests PRIVATE
        ubahn_app_lib
        Qt6::Test
    )
    ubahn_set_warnings(ubahn_app_mock_tests)
    add_test(NAME ubahn_app_mock_tests COMMAND ubahn_app_mock_tests)
    set_tests_properties(ubahn_app_mock_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

set_tests_properties(
    ubahn_app_shell_tests
    ubahn_app_line_view_tests
    ubahn_app_board_tests
    ubahn_app_playback_panel_tests
    ubahn_app_setup_editor_tests
    PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
