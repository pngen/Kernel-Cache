# KernelCache benchmarks.
add_executable(kc_bench bench.cpp)
target_link_libraries(kc_bench PRIVATE KernelCache)
add_test(NAME kc_bench COMMAND kc_bench)
