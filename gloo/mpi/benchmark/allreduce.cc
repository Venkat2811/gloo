#include "gloo/mpi/benchmark/common.h"

#include <mpi.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gloo/allreduce.h"
#include "gloo/math.h"

namespace {

std::vector<gloo::mpi_bench::BenchRow> runAllreduce(
    const gloo::mpi_bench::BenchOptions& options,
    const gloo::mpi_bench::BenchmarkSession& session) {
  std::vector<gloo::mpi_bench::BenchRow> rows;

  for (const auto bytes : options.sizes) {
    if (bytes == 0 || bytes % sizeof(uint32_t) != 0) {
      throw std::invalid_argument(
          "allreduce sizes must be positive multiples of 4 bytes");
    }

    const auto elements = bytes / sizeof(uint32_t);
    std::vector<uint32_t> input(elements, static_cast<uint32_t>(session.rank + 1));
    std::vector<uint32_t> output(elements, 0);

    gloo::AllreduceOptions benchOpts(session.context);
    benchOpts.setInput<uint32_t>(input.data(), elements);
    benchOpts.setOutput<uint32_t>(output.data(), elements);
    benchOpts.setReduceFunction(
        static_cast<void (*)(void*, const void*, const void*, size_t)>(
            &gloo::sum<uint32_t>));
    benchOpts.setAlgorithm(gloo::AllreduceOptions::Algorithm::RING);

    MPI_Barrier(MPI_COMM_WORLD);
    for (int i = 0; i < options.warmup; i++) {
      gloo::allreduce(benchOpts);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    std::vector<long long> localSamples(options.iterations);
    for (int i = 0; i < options.iterations; i++) {
      const auto start = std::chrono::steady_clock::now();
      gloo::allreduce(benchOpts);
      const auto end = std::chrono::steady_clock::now();
      localSamples[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    if (options.verify) {
      const uint32_t expected = static_cast<uint32_t>(
          (session.size * (session.size + 1)) / 2);
      bool localOk = true;
      for (const auto value : output) {
        if (value != expected) {
          localOk = false;
          break;
        }
      }
      int localOkInt = localOk ? 1 : 0;
      int globalOkInt = 0;
      MPI_Allreduce(&localOkInt, &globalOkInt, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
      if (globalOkInt == 0) {
        throw std::runtime_error("allreduce verification failed");
      }
    }

    auto reduced = gloo::mpi_bench::reduceMaxSamples(localSamples);
    if (session.rank == 0) {
      rows.push_back(gloo::mpi_bench::buildRow(
          bytes,
          options.iterations,
          reduced,
          static_cast<double>(bytes)));
    }
  }

  return rows;
}

} // namespace

int main(int argc, char** argv) {
  return gloo::mpi_bench::runBenchmarkMain(
      argc,
      argv,
      "mpi_bench_allreduce",
      "MPI-launched Gloo allreduce latency sweep using unbound-buffer options",
      runAllreduce);
}
