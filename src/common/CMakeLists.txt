add_library(agv-common STATIC
    cliparser.h
    cliparser.cpp
    dbustypes.h
    dbustypes.cpp
    siteexclusion.h
    siteexclusion.cpp
)

target_include_directories(agv-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(agv-common PUBLIC Qt6::Core Qt6::DBus Qt6::Network)
target_compile_options(agv-common PRIVATE -Wall -Wextra -Wpedantic)
