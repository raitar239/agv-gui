cmake_minimum_required(VERSION 3.22)

project(agv-gui
    VERSION 0.1.0
    DESCRIPTION "GUI and per-app exclusions for AdGuard VPN CLI"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)
include(CTest)

# Qt 6.4 is the oldest supported version (Debian 12, Ubuntu 24.04).
find_package(Qt6 6.4 REQUIRED COMPONENTS Core DBus Network)

option(AGV_BUILD_GUI "Build the agv-gui window and tray" ON)

add_subdirectory(src/common)
add_subdirectory(src/daemon)
add_subdirectory(src/run)
if(AGV_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Gui Widgets Quick QuickControls2)
    add_subdirectory(src/gui)
endif()
add_subdirectory(data)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
