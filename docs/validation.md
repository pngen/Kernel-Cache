# Validation

Every artifact must pass validation before eligibility. `KernelValidator`
performs integrity-hash, format, architecture, ABI, and metadata checks, then a
backend-specific execution check.

For CUDA this means a real module load, real device-memory allocation, real
kernel launch, synchronisation, a result copy-back, a comparison against a
deterministic CPU reference, and resource release. An artifact that compiles but
fails execution validation is not a cacheable valid artifact.
