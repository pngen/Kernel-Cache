# Distributed coordinator / worker / client executables + protocol proof.
add_executable(kc_coordinator coordinator.cpp)
target_link_libraries(kc_coordinator PRIVATE KernelCache)
target_include_directories(kc_coordinator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(kc_worker worker.cpp)
target_link_libraries(kc_worker PRIVATE KernelCache)
target_include_directories(kc_worker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(kc_client client.cpp)
target_link_libraries(kc_client PRIVATE KernelCache)
target_include_directories(kc_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME kc_dist_atomic COMMAND ${CMAKE_CURRENT_BINARY_DIR}/../tests/kc_dist_atomic.exe)