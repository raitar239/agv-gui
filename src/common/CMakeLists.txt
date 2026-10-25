add_library(agv-common STATIC
    cliparser.h
    cliparser.cpp
)

target_include_directories(agv-common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(agv-common PUBLIC Qt6::Core)
target_compile_options(agv-common PRIVATE -Wall -Wextra -Wpedantic)
