# Testing and validation

The suite covers unit behavior (key/compatibility/lifecycle/lease), a concurrency
suite with a deadlock and reentrancy audit, an adversarial suite (malformed,
corrupt, truncated, stale, duplicate, impossible generation), a fixed-seed
property suite that continuously asserts invariants, a persistence/recovery
suite, a real CUDA suite on the supported hardware, and a distributed
coordinator/worker/client proof. The build uses `/W4 /WX` and all suite runs
are expected to succeed with zero warnings.
