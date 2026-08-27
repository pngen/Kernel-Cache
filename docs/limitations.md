# Known limitations

- On a build without the CUDA backend, GPU execution is unavailable and the
  CUDA suite skips cleanly.
- Device-resident executable module memory is estimated; exact consumption is
  not directly queryable through the exposed driver functions and is labelled
  as estimated.
- The compatibility model is deterministic and best-effort for the dimensions it
  models; a backend or runtime must not rely on reuse for kernels whose
  correctness depends on a dimension not encoded in the key.
- Avoided-compile-time is derived, not measured.
