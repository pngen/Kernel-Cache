# Framed protocol

The distributed protocol uses fixed-width frames:
`[frame_len u32][proto_ver u16][msg_type u16][payload]`. Frames have a hard
maximum size, strict decode validation, semantic validation, and lossless 64-bit
identity marshalling. Messages carry authoritative state
(`CoordinatorEpoch`, `WorkerId`, `WorkerBootId`, `CacheGeneration`,
`ArtifactId`, `ArtifactGeneration`, `BuildAttemptId`, `RequestId`) so stale
operations are rejected deterministically.
