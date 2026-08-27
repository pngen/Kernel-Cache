# Invalidation

Invalidation is supported by `ArtifactId`, `KernelId`, compatibility key,
operation, compiler/runtime generation, architecture, model/operator revision,
namespace, or a predicate. Invalidation immediately blocks new leases, preserves
active leases until they are safely released (unless hard semantics explicitly
require otherwise), retires loaded device state at a safe point, and prevents a
stale lookup from returning an old artifact after invalidation commits.
