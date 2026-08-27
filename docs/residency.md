# Residency

Residency is modelled as MetadataOnly, PersistentStorage, HostResident, and
DeviceResident. Movement is explicit:

```
PersistentStorage -> HostResident -> DeviceResident -> HostResident -> PersistentStorage
```

Device-resident executable state is tracked separately from persisted artifact
bytes. Budgets govern host and device footprints, and eviction is cost-aware
(recency, frequency, compile cost, reload cost, size, pin count, namespace
priority) with deterministic tie-breaking. An actively leased artifact is never
evicted in a way that invalidates an executing handle.
