#include "gloo/mpi/benchmark/common.h"

#include <mpi.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

constexpr uint64_t kForwardTag = 0xB001;
constexpr uint64_t kReturnTag = 0xB002;

void fillPattern(std::vector<uint8_t>& buffer) {
  for (size_t i = 0; i < buffer.size(); i++) {
    buffer[i] = static_cast<uint8_t>((i * 29 + 3) & 0xff);
  }
}

std::vector<gloo::mpi_bench::BenchRow> runPingPong(
    const gloo::mpi_bench::BenchOptions& options,
    const gloo::mpi_bench::BenchmarkSession& session) {
  if (session.size != 2) {
    throw std::invalid_argument("pingpong benchmark requires exactly 2 ranks");
  }

  std::vector<gloo::mpi_bench::BenchRow> rows;

  for (const auto bytes : options.sizes) {
    std::vector<uint8_t> sendBuffer(bytes);
    std::vector<uint8_t> recvBuffer(bytes, 0);
    fillPattern(sendBuffer);

    auto send = session.context->createUnboundBuffer(sendBuffer.data(), sendBuffer.size());
    auto recv = session.context->createUnboundBuffer(recvBuffer.data(), recvBuffer.size());

    MPI_Barrier(MPI_COMM_WORLD);
    for (int i = 0; i < options.warmup; i++) {
      if (session.rank == 0) {
        send->send(1, kForwardTag);
        recv->recv(1, kReturnTag);
        send->waitSend();
        recv->waitRecv();
      } else {
        recv->recv(0, kForwardTag);
        recv->waitRecv();
        recv->send(0, kReturnTag);
        recv->waitSend();
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    std::vector<long long> samples;
    if (session.rank == 0) {
      samples.resize(options.iterations);
    }
    for (int i = 0; i < options.iterations; i++) {
      if (session.rank == 0) {
        const auto start = std::chrono::steady_clock::now();
        send->send(1, kForwardTag);
        recv->recv(1, kReturnTag);
        send->waitSend();
        recv->waitRecv();
        const auto end = std::chrono::steady_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      } else {
        recv->recv(0, kForwardTag);
        recv->waitRecv();
        recv->send(0, kReturnTag);
        recv->waitSend();
      }
    }

    if (options.verify) {
      bool localOk = true;
      if (session.rank == 0) {
        localOk = std::equal(recvBuffer.begin(), recvBuffer.end(), sendBuffer.begin());
      }
      int localOkInt = localOk ? 1 : 0;
      int globalOkInt = 0;
      MPI_Allreduce(&localOkInt, &globalOkInt, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
      if (globalOkInt == 0) {
        throw std::runtime_error("pingpong verification failed");
      }
    }

    if (session.rank == 0) {
      rows.push_back(gloo::mpi_bench::buildRow(
          bytes,
          options.iterations,
          samples,
          static_cast<double>(bytes) * 2.0));
    }
  }

  return rows;
}

} // namespace

int main(int argc, char** argv) {
  return gloo::mpi_bench::runBenchmarkMain(
      argc,
      argv,
      "mpi_bench_pingpong",
      "MPI-launched 2-rank Gloo round-trip benchmark using unbound buffers",
      runPingPong);
}
