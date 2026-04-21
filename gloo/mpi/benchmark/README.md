# MPI Benchmark Notes

This directory contains MPI-launched multiprocess benchmarks for Gloo and a prototype
Myelon/disruptor shared-memory binding.

## Gloo baseline

Configure and build with MPI plus a transport supported on the local host. On macOS in
this workspace that means `libuv`.

```bash
cmake -S . -B build-mpi-uv \
  -DUSE_MPI=ON \
  -DUSE_LIBUV=ON \
  -DBUILD_BENCHMARK=ON
cmake --build build-mpi-uv -j8 --target mpi_bench_pingpong mpi_bench_broadcast mpi_bench_allreduce
```

Run the transport-only pingpong microbenchmark with two ranks:

```bash
mpirun -n 2 ./build-mpi-uv/gloo/mpi/benchmark/mpi_bench_pingpong \
  --sizes=4,64,1KiB,16KiB,256KiB,1MiB \
  --warmup=20 \
  --iterations=100
```

## Myelon SHM prototype

`mpi_bench_myelon_pingpong` is not a real Gloo transport backend yet. It uses MPI only
for process launch and barriers, then sends the benchmark payloads through the Rust
`myelon-gloo-ffi` C ABI backed by `disruptor-mp` shared memory rings.

Point CMake at a sibling or external `myelon-playground` checkout that contains
`crates/myelon-gloo-ffi`:

```bash
cmake -S . -B build-mpi-uv \
  -DUSE_MPI=ON \
  -DUSE_LIBUV=ON \
  -DBUILD_BENCHMARK=ON \
  -DMYELON_PLAYGROUND_ROOT=/path/to/myelon-playground
cmake --build build-mpi-uv -j8 --target mpi_bench_myelon_pingpong
```

Run the SHM round-trip benchmark:

```bash
mpirun -n 2 ./build-mpi-uv/gloo/mpi/benchmark/mpi_bench_myelon_pingpong \
  --sizes=4,64,1KiB,16KiB,256KiB,1MiB \
  --warmup=20 \
  --iterations=100
```

## Binding shape

The Rust side exposes a deliberately small C ABI:

- create a producer for one shared-memory ring
- attach a consumer to an existing ring
- publish one payload into the ring
- receive one payload with a blocking spin-wait
- select a bucketed slot size for the requested payload

That bucketed payload API matters. A fixed 1 MiB slot for every message made small
message latency unrepresentative. The current binding chooses from `64 B`, `1 KiB`,
`4 KiB`, `64 KiB`, `256 KiB`, and `1 MiB` slots so the stride matches the payload
class closely enough for useful prototype numbers.
