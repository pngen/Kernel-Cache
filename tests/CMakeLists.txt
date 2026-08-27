# KernelCache tests.
function(kc_add_test name src)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE KernelCache)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

kc_add_test(kc_unit_core unit_core.cpp)
kc_add_test(kc_concurrency concurrency.cpp)
kc_add_test(kc_adversarial adversarial.cpp)
kc_add_test(kc_property property.cpp)
kc_add_test(kc_persistence persistence_recovery.cpp)
kc_add_test(kc_cuda cuda.cpp)
kc_add_test(kc_dist_protocol dist_protocol.cpp)

# Atomic multiprocess stale-authority proof (needs coordinator + worker exes).
add_executable(kc_dist_atomic distributed_atomic.cpp)
target_link_libraries(kc_dist_atomic PRIVATE KernelCache)
target_include_directories(kc_dist_atomic PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../distributed
)
target_compile_definitions(kc_dist_atomic PRIVATE
  KC_DIST_DIR="${CMAKE_BINARY_DIR}/distributed"
)
add_test(NAME kc_dist_atomic COMMAND kc_dist_atomic)

add_test(NAME kc_all COMMAND kc_unit_core)
