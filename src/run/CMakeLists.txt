add_executable(agv-run agv-run.c)
set_target_properties(agv-run PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(agv-run PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS agv-run DESTINATION ${CMAKE_INSTALL_BINDIR})
