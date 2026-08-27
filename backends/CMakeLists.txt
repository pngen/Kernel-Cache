# Backends.
# CPU synthetic backend (always built).
target_sources(KernelCache PRIVATE
  cpu/cpu_backend.cpp
)
if(KERNELCACHE_CUDA)
  target_sources(KernelCache PRIVATE
    cuda/cuda_backend.cpp
  )
  # CUDA driver is loaded dynamically at runtime; no CUDA import lib is needed in
  # the link interface. Provide the nvcc path for offline compilation.
  target_compile_definitions(KernelCache PRIVATE
    KC_HAS_CUDA=1
    KC_CUDA_NVCC="${CUDA_nvcc}"
  )
endif()
