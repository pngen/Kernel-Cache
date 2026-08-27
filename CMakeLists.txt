cmake_minimum_required(VERSION 3.24)

project(KernelCache
  VERSION 1.0.0
  DESCRIPTION "Vendor-neutral runtime for caching compiled and executable AI kernels"
  LANGUAGES CXX)

option(KERNELCACHE_CUDA "Build the real CUDA backend (requires CUDA toolkit + driver)" ON)
option(KERNELCACHE_BUILD_TESTS "Build the test suite" ON)
option(KERNELCACHE_BUILD_EXAMPLES "Build the example programs" ON)
option(KERNELCACHE_BUILD_BENCHMARKS "Build the benchmark suite" ON)
option(KERNELCACHE_BUILD_CLI "Build the command-line interface (kc)" ON)
option(KERNELCACHE_BUILD_DISTRIBUTED "Build the distributed coordinator/worker/clients" ON)
option(KERNELCACHE_SANITIZERS "Build with address/undefined sanitizers (GCC/Clang only)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  add_compile_options(/W4 /WX /permissive-)
  add_compile_options(/EHsc)
  add_compile_options(/utf-8)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include(CTest)

if(KERNELCACHE_CUDA)
  find_package(CUDAToolkit QUIET)
  if(NOT CUDAToolkit_FOUND)
    message(STATUS "KernelCache: CUDA toolkit not found -> building CPU-only backend")
    set(KERNELCACHE_CUDA OFF)
  else()
    message(STATUS "KernelCache: CUDA toolkit found (v${CUDAToolkit_VERSION})")
  endif()
endif()

add_subdirectory(src)
add_subdirectory(backends)

if(KERNELCACHE_BUILD_CLI)
  add_subdirectory(cli)
endif()
if(KERNELCACHE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
if(KERNELCACHE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(KERNELCACHE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
if(KERNELCACHE_BUILD_DISTRIBUTED)
  add_subdirectory(distributed)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS KernelCache
  EXPORT KernelCacheTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/kernelcache
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT KernelCacheTargets
  FILE KernelCacheTargets.cmake
  NAMESPACE KernelCache::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelCache)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/KernelCacheConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/KernelCacheConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelCache)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/KernelCacheConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/KernelCacheConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/KernelCacheConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/KernelCache)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VersionHeader.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/kc_version.hpp
  @ONLY)
