# KernelCache CLI.
add_executable(kc kc.cpp)
target_link_libraries(kc PRIVATE KernelCache)
add_test(NAME kc_cli_stats COMMAND kc stats)