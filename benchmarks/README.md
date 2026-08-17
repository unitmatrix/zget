# Benchmarks

Benchmarks are intentionally kept out of the correctness suite. Generate a ZIP
with `generate.py`, serve it with a Range-capable HTTP server, then record:

- request count and transferred bytes;
- time to first output byte and total throughput;
- CPU time and peak resident memory;
- targets near the start, middle, and end of the Central Directory, plus misses;
- large STORE and DEFLATE payloads.

```sh
python3 benchmarks/generate.py archive.zip --entries 100000
```

Use `--entries 1000000` for the million-entry scale test; generated archives
must not be committed.
