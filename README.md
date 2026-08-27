# Kernel Cache

Kernel Cache is an open-source, vendor-neutral runtime for caching compiled and
executable AI kernels, compatibility-safe lookup, reuse, invalidation, residency,
persistence, and accelerator-aware dispatch across heterogeneous inference
infrastructure.

The governing question it answers is:

> Which compiled kernel artifact may be safely reused for this execution request,
> under what compatibility constraints, where should it reside, when should it be
> invalidated or evicted, and how can reuse occur without violating correctness
> across models, shapes, devices, runtimes, or software revisions?

Kernel Cache is explicitly **not** a file cache, a generic build cache, a CUDA
kernel demo, a JIT wrapper, a shader-cache clone, a benchmark shell, or a
metadata index. It is a runtime boundary for executable-kernel reuse inside AI
inference and accelerator infrastructure.

## Core Concepts

- **KernelCompatibilityKey** - a canonical, typed, deterministic identity that
  captures every correctness-affecting input. It is encoded canonically and hashed
  with SHA-256; the full typed metadata is preserved for explainability.
- **Compatibility engine** - evaluates a request key against candidate artifact
  keys and produces a structured
  `KernelCompatibilityDecision` (exact, compatible-with-dynamic-shape,
  compatible-with-runtime-validation, or an explicit incompatible reason).
- **Artifact lifecycle** - an explicit state machine
  (`Discovered -> Building -> Built -> Validating -> Valid -> Loading ->
  ResidentHost -> ResidentDevice -> Persisted -> ... -> Retired -> Terminal`)
  in which every transition is validated.
- **Residency tiers** - MetadataOnly, PersistentStorage, HostResident,
  DeviceResident, with explicit movement and budget governance.
- **Build coordination** - single-flight builds: N concurrent misses for the same
  key produce exactly one compile; waiters subscribe to the shared result.
- **Leases** - `KernelLease` pins eligibility for the duration of use, released
  idempotently, never underflowing.
- **Persistence & recovery** - checksummed, versioned, crash-safe on-disk
  storage with atomic temp-write + rename, corruption/truncation rejection, and
  orphan-temp cleanup.
- **Generation authority** - builds, loads, and validations are tied to
  coordinated `CacheGeneration` / `ArtifactGeneration` /
  `BuildAttemptId`; completions from obsolete attempts never replace a newer
  artifact.
- **Distributed coordination** - a real framed-TCP coordinator/worker/client
  protocol carrying authoritative generation state across process boundaries.

## Compatibility-Safe Lookup

A cache hit is a correctness decision. A lookup:

1. Normalizes the request.
2. Constructs a typed `KernelCompatibilityKey`.
3. Queries the candidate index.
4. Applies the `CompatibilityPolicy`.
5. Validates generation and artifact state.
6. Validates runtime and device constraints.
7. Validates residency requirements.
8. Selects the best compatible candidate.
9. Acquires a lease.
10. Promotes/loads if needed.
11. Returns an execution-ready handle.
12. Releases/updates accounting after use.

A lookup never returns an artifact solely because a filename, operator name, or
hash prefix matches. An incompatible request is never silently coerced.

## Backends

Kernel Cache is vendor-neutral. Backends implement three interfaces:
`KernelBuilder`, `KernelValidator`, and `KernelLoader`.

- **CPU synthetic backend** (always available) - a deterministic reference
  execution and validation path that requires no accelerator.
- **CUDA backend** - compiles CUDA source to a cubin at runtime via NVRTC, loads
  it with the CUDA driver API, launches a real kernel, synchronizes, copies the
  result back, and compares it against a deterministic CPU reference. On the
  supported Blackwell target it exercises vector transform, reduction, and
  elementwise operations with real module load, execution, and device eviction
  and reload.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The library builds with `/W4 /WX` on MSVC (zero warnings). Enable or disable
the CUDA backend with `-DKERNELCACHE_CUDA=ON/OFF`.

### Use as a downstream dependency

```cmake
find_package(KernelCache CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE KernelCache::KernelCache)
```

## Public API overview

`KernelCache`, `KernelCacheConfig`, `KernelLookupRequest`,
`KernelLookupResult`, `KernelCompatibilityKey`,
`KernelCompatibilityDecision`, `KernelDescriptor`, `ArtifactDescriptor`,
`KernelBuilder`, `KernelValidator`, `KernelLoader`, `KernelLease`,
`KernelReservation`, `ResidencyPolicy`, `EvictionPolicy`,
`InvalidationRequest`, `CacheGeneration`, `ArtifactGeneration`,
`BuildAttemptId`, `DeviceDescriptor`, `RuntimeDescriptor`,
`PersistenceStore`, `Clock`, `Snapshot`, `Stats`, `Event`,
`Explain`, `Result<T>`, `Result<void>`, and a structured `ErrorCode`.

The full public header is `<kernelcache/kernelcache.hpp>`.

## Examples

```
examples/ex01_basic_hit            # basic cache hit / miss + build
examples/ex02_miss_build           # miss then single-flight build
examples/ex03_compat_rejection     # incompatible shape/dtype is never a hit
examples/ex04_shape_specialized    # shape-specialized artifacts coexist
examples/ex05_datatype_specialized # datatype-specialized artifacts coexist
examples/ex06_single_flight        # concurrent same-key single-flight build
examples/ex07_lease_invalidate     # lease + safe invalidation
examples/ex08_evict_reload         # eviction then reload
examples/ex09_persistent_recovery  # persistence + recovery
examples/ex10_corruption_rejection # corrupt persisted artifact rejected
examples/ex11_eviction_score       # cost-aware eviction candidates
examples/ex12_distributed_client   # connect a client to a running coordinator
```

## CLI

```
build/cli/kc stats
build/cli/kc build vec_add 1024
build/cli/kc lookup vec_add 1024
build/cli/kc bench 100000
```

## Distributed serve / worker

```
build/distributed/kc_coordinator 41000 <cache_root>
build/distributed/kc_worker 127.0.0.1 41000 1 256 1
build/distributed/kc_client 127.0.0.1 41000 vec_add 1024
```

## Observability and explainability

`KernelCache` exposes `stats()`, `snapshot()`, `events()`,
`explain(artifact)`, and `eviction_candidates()`, so operators can answer why
a lookup was a hit or miss, why an artifact was chosen, why it was invalidated,
evicted, pinned, or promoted, and why a stale build was rejected.

## Documentation

See the `docs/` directory for the architecture, artifact lifecycle,
compatibility-key, compilation, validation, residency, invalidation,
persistence-format, framed-protocol, testing, benchmark-methodology, and
limitations documents.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
