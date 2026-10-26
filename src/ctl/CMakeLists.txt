# The period summary is shared with the window; it needs only QtCore.
add_executable(agvctl main.cpp ${PROJECT_SOURCE_DIR}/src/gui/statistics.cpp)
target_include_directories(agvctl PRIVATE ${PROJECT_SOURCE_DIR}/src/gui)
target_link_libraries(agvctl PRIVATE agv-common Qt6::Core Qt6::DBus)
target_compile_definitions(agvctl PRIVATE AGV_VERSION="${PROJECT_VERSION}")
target_compile_options(agvctl PRIVATE -Wall -Wextra)

install(TARGETS agvctl DESTINATION ${CMAKE_INSTALL_BINDIR})
