# Benchmark methodology

Benchmarks measure real workloads: key canonicalisation + SHA-256, cold build +
validation, warm lookup hit, single-flight under concurrency, persistence write,
and mixed hit ratios. Warm lookup throughput is reported as lookups/second;
cold build latency and device-load latency are reported in milliseconds. Avoided
compile time is reported only as a derived figure from measured cold vs warm
runs, never asserted as a measured saving.
