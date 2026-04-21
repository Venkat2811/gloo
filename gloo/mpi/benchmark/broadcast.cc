#include "gloo/mpi/benchmark/common.h"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "gloo/broadcast.h"

namespace {

void fillPattern(std::vector<uint8_t>& buffer) {
  for (size_t i = 0; i < buffer.size(); i++) {
    buffer[i] = static_cast<uint8_t>((i * 131 + 17) & 0xff);
  }
}

std::vector<gloo::mpi_bench::BenchRow> runBroadcast(
    const gloo::mpi_bench::BenchOptions& options,
    const gloo::mpi_bench::BenchmarkSession& session) {
  std::vector<gloo::mpi_bench::BenchRow> rows;

  for (const auto bytes : options.sizes) {
    std::vector<uint8_t> input(bytes);
    std::vector<uint8_t> output(bytes, 0);
    fillPattern(input);

    gloo::BroadcastOptions benchOpts(session.context);
    benchOpts.setRoot(options.root);
    if (session.rank == options.root) {
      benchOpts.setInput<uint8_t>(input.data(), input.size());
    }
    benchOpts.setOutput<uint8_t>(output.data(), output.size());

    MPI_Barrier(MPI_COMM_WORLD);
    for (int i = 0; i < options.warmup; i++) {
      gloo::broadcast(benchOpts);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    std::vector<long long> localSamples(options.iterations);
    for (int i = 0; i < options.iterations; i++) {
      const auto start = std::chrono::steady_clock::now();
      gloo::broadcast(benchOpts);
      const auto end = std::chrono::steady_clock::now();
      localSamples[i] =
          std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    if (options.verify) {
      const bool localOk = std::equal(output.begin(), output.end(), input.begin());
      int localOkInt = localOk ? 1 : 0;
      int globalOkInt = 0;
      MPI_Allreduce(&localOkInt, &globalOkInt, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
      if (globalOkInt == 0) {
        throw std::runtime_error("broadcast verification failed");
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
      "mpi_bench_broadcast",
      "MPI-launched Gloo broadcast latency sweep using unbound-buffer options",
      runBroadcast);
}
