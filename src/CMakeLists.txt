# Core KernelCache library.
add_library(KernelCache STATIC
  sha256.cpp
  canonical.cpp
  descriptors.cpp
  identifiers.cpp
  names.cpp
  key.cpp
  compatibility.cpp
  lifecycle.cpp
  state_machine.cpp
  artifact_store.cpp
  index.cpp
  persistence.cpp
  recovery.cpp
  build.cpp
  validation.cpp
  residency.cpp
  explain.cpp
  stats.cpp
  cache.cpp
  leases.cpp
  clock.cpp
  backend_registry.cpp
  distributed.cpp
)

target_include_directories(KernelCache PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:include>
)
target_include_directories(KernelCache PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_compile_features(KernelCache PUBLIC cxx_std_20)
target_compile_definitions(KernelCache PRIVATE KC_BUILDING_LIBRARY=1)

if(KERNELCACHE_CUDA)
  target_compile_definitions(KernelCache PRIVATE KC_HAS_CUDA=1)
  target_link_libraries(KernelCache PRIVATE ${CMAKE_DL_LIBS})
endif()

set_property(TARGET KernelCache PROPERTY POSITION_INDEPENDENT_CODE ON)