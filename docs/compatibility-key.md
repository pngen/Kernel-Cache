# Compatibility key

`KernelCompatibilityKey` encodes every correctness-affecting input:
operation identity, artifact format, vendor/architecture/compute capability,
runtime and compiler identity, ABI and interface signature, datatypes, layouts,
rank, shape specialization, symbolic constraints, alignment, shared memory,
launch ABI, specialization flags, model/operator revision, and quantization.

The key is canonicalised into a deterministic binary encoding and hashed with
SHA-256. Identical semantic inputs produce identical keys; a change in any
correctness-affecting field produces a different identity. Once
`finalize()` is called the key is frozen; the full typed metadata is preserved
for explainability. The canonical bytes can be decoded losslessly with
`from_canonical` (used by persistence and the distributed protocol).
