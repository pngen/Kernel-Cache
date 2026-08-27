# Artifact lifecycle

Each artifact progresses through an explicit state machine:

```
Discovered -> Building -> Built -> Validating -> Valid -> Loading
           -> ResidentHost -> ResidentDevice -> Persisted -> Leasing -> InUse
           -> DemotionPending -> EvictionPending -> EvictedDevice/EvictedHost
           -> Invalidated | Corrupt | Failed | Retired | Terminal
```

Transitions are validated against a whitelist. An invalidated or corrupt artifact
never re-enters eligibility without being rebuilt under a new generation. A
retired artifact is never silently reused. Mutable accounting (access counters,
residency state) is kept separate from immutable artifact identity; executable
bytes are never mutated under an existing `ArtifactId`.
