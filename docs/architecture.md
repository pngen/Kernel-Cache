# Architecture

Kernel Cache is a vendor-neutral runtime. The system is organised around a small
number of cohesion boundaries:

- **Identity and keys** - `KernelCompatibilityKey`, `KernelId`,
  `ArtifactId`, and the generation types.
- **Compatibility engine** - deterministic typed comparison between a request
  and a candidate.
- **Artifact store** - the authoritative canonical state (artifacts and their
  secondary indices), guarded by a shared mutex.
- **Build coordination** - single-flight builds that run outside every cache lock.
- **Residency manager** - load/unload, tier movement, budget enforcement.
- **Persistence store** - crash-safe on-disk artifact storage.
- **Backends** - `KernelBuilder`, `KernelValidator`, `KernelLoader`.

## Locking discipline

The implementation avoids deadlocks and lock-reentrancy by never holding a cache
lock while doing backend work (compile, validate, load/unload) or disk I/O. State
transitions take the master lock briefly; backends run without it. Locks are
acquired in a single consistent order.

## Threading model

A single `KernelCache` instance is safe for concurrent lookup, miss, single-flight
build, validation, lease, release, invalidation, eviction, persistence, reload,
and observability from many threads.
