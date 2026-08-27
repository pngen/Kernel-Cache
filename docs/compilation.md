# Compilation and build

Builds run through `KernelBuilder`. The CPU backend produces a deterministic
synthetic executable workload. The CUDA backend compiles CUDA source to a cubin
at runtime via NVRTC, with no host-toolchain environment required. A build is a
real compilation, never a copy of prebuilt bytes.

Builds are single-flight: concurrent misses for the same key produce one owner
that compiles, while waiters subscribe to the shared result. A failed build
propagates consistently, and a retry creates a new build-attempt identity.
