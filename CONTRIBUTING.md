# Contributing to Kernel Cache

Thank you for your interest in contributing. Kernel Cache is licensed under the
Apache License 2.0 and accepts contributions without a Contributor License
Agreement (CLA).

## Scope

Kernel Cache is a vendor-neutral runtime for caching compiled and executable AI
kernels. Contributions should improve the core runtime, its compatibility model,
persistence, residency, or the vendor-neutral backend interfaces.

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The project builds with `/W4 /WX` (MSVC) and treats warnings as errors.

## Guidelines

- Keep the public API under `include/kernelcache` stable and documented.
- New backends implement `KernelBuilder`, `KernelValidator`, and `KernelLoader`.
- Never mutate executable bytes under an existing `ArtifactId`.
- Preserve the deterministic canonical key encoding.
- Add tests for new behavior; the project uses runtime `CHECK` macros so tests
  are effective in Release builds.

## License

By contributing you agree that your contribution is licensed under the Apache
License 2.0.
