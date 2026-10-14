set(UBAHN_APP_SOURCES
    AppSession.cpp
    AppSession.h
    BoardWidget.cpp
    BoardWidget.h
    LineEditor.cpp
    LineEditor.h
    LineView.cpp
    LineView.h
    MainWindow.cpp
    MainWindow.h
    MonitoringPanel.cpp
    MonitoringPanel.h
    PlaybackController.cpp
    PlaybackController.h
    PlaybackPanel.cpp
    PlaybackPanel.h
    RuntimeScheduleEditor.cpp
    RuntimeScheduleEditor.h
    ScheduleEditor.cpp
    ScheduleEditor.h
    SetupDialog.cpp
    SetupDialog.h
    SimulationState.cpp
    SimulationState.h
    SummaryWidget.cpp
    SummaryWidget.h
)

if(TARGET ubahn_worker)
    set(UBAHN_APP_BACKEND_TARGET ubahn_worker)
else()
    list(APPEND UBAHN_APP_SOURCES
        MockApi.cpp
        MockSamples.cpp
        MockSamples.h
    )
    set(UBAHN_APP_BACKEND_TARGET ubahn_api)
endif()

qt_add_library(ubahn_app_lib STATIC ${UBAHN_APP_SOURCES})
target_include_directories(ubahn_app_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ubahn_app_lib PUBLIC
    ubahn_api
    ${UBAHN_APP_BACKEND_TARGET}
    Qt6::Widgets
)
ubahn_set_warnings(ubahn_app_lib)

qt_add_executable(ubahn_app main.cpp)
target_link_libraries(ubahn_app PRIVATE ubahn_app_lib)
ubahn_set_warnings(ubahn_app)
