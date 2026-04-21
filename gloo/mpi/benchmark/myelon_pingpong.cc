#include "gloo/mpi/benchmark/common.h"

#include <mpi.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "gloo/transport/myelon/binding.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

constexpr size_t kRingDepth = 64;

std::string make_segment_name(uint64_t seed, char suffix) {
  std::ostringstream oss;
  oss << "mg" << std::hex << std::nouppercase << std::setw(8) << std::setfill('0')
      << static_cast<uint32_t>(seed & 0xffffffffu) << suffix;
  return oss.str();
}

void fill_pattern(std::vector<uint8_t>& buffer) {
  for (size_t i = 0; i < buffer.size(); i++) {
    buffer[i] = static_cast<uint8_t>((i * 17 + 5) & 0xff);
  }
}

int run_benchmark(int argc, char** argv) {
  const auto options =
      gloo::mpi_bench::parseBenchOptions(argc, argv, "mpi_bench_myelon_pingpong");
  if (options.root < 0 || options.root > 1) {
    throw std::invalid_argument("root must be 0 or 1 for 2-rank pingpong");
  }

  int rank = -1;
  int size = 0;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  if (size != 2) {
    throw std::invalid_argument("myelon pingpong benchmark requires exactly 2 ranks");
  }

  const int initiator = options.root;
  const int responder = initiator == 0 ? 1 : 0;
  if (rank != initiator && rank != responder) {
    throw std::invalid_argument("unexpected MPI rank layout");
  }

  const auto max_payload = gloo::transport::myelon::maxPayloadBytes();
  for (const auto bytes : options.sizes) {
    if (bytes > max_payload) {
      std::ostringstream oss;
      oss << "requested size " << bytes << " exceeds binding max payload " << max_payload;
      throw std::invalid_argument(oss.str());
    }
  }

  uint64_t seed = 0;
  if (rank == 0) {
    const auto now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    seed = now ^ (static_cast<uint64_t>(::getpid()) << 21);
  }
  MPI_Bcast(&seed, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

  const auto init_to_resp = make_segment_name(seed, 'a');
  const auto resp_to_init = make_segment_name(seed, 'b');
  const char* consumer_id = rank == initiator ? "c0" : "c1";

  std::vector<gloo::mpi_bench::BenchRow> rows;
  for (size_t index = 0; index < options.sizes.size(); index++) {
    const auto bytes = options.sizes[index];
    const auto bucket_bytes = gloo::transport::myelon::bucketPayloadBytes(bytes);

    const auto outbound_name =
        rank == initiator ? make_segment_name(seed ^ bytes ^ index, 'a')
                          : make_segment_name(seed ^ bytes ^ index, 'b');
    const auto inbound_name =
        rank == initiator ? make_segment_name(seed ^ bytes ^ index, 'b')
                          : make_segment_name(seed ^ bytes ^ index, 'a');

    gloo::transport::myelon::Producer outbound_producer(
        outbound_name, kRingDepth, bucket_bytes);

    MPI_Barrier(MPI_COMM_WORLD);

    gloo::transport::myelon::Consumer inbound_consumer(
        inbound_name, kRingDepth, bucket_bytes, consumer_id);

    MPI_Barrier(MPI_COMM_WORLD);

    std::vector<uint8_t> send_buffer(bytes);
    std::vector<uint8_t> recv_buffer(bytes);
    size_t received_len = 0;
    fill_pattern(send_buffer);

    MPI_Barrier(MPI_COMM_WORLD);
    for (int i = 0; i < options.warmup; i++) {
      if (rank == initiator) {
        outbound_producer.publish(send_buffer.data(), send_buffer.size());
        received_len = inbound_consumer.receiveBlocking(recv_buffer.data(), recv_buffer.size());
      } else {
        received_len = inbound_consumer.receiveBlocking(recv_buffer.data(), recv_buffer.size());
        outbound_producer.publish(recv_buffer.data(), received_len);
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    std::vector<long long> samples;
    if (rank == initiator) {
      samples.resize(options.iterations);
    }

    for (int i = 0; i < options.iterations; i++) {
      if (rank == initiator) {
        const auto start = std::chrono::steady_clock::now();
        outbound_producer.publish(send_buffer.data(), send_buffer.size());
        received_len = inbound_consumer.receiveBlocking(recv_buffer.data(), recv_buffer.size());
        const auto end = std::chrono::steady_clock::now();
        samples[i] =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
      } else {
        received_len = inbound_consumer.receiveBlocking(recv_buffer.data(), recv_buffer.size());
        outbound_producer.publish(recv_buffer.data(), received_len);
      }
    }

    if (options.verify && rank == initiator) {
      if (received_len != send_buffer.size()) {
        throw std::runtime_error("echo length mismatch");
      }
      if (!std::equal(recv_buffer.begin(), recv_buffer.end(), send_buffer.begin())) {
        throw std::runtime_error("echo payload mismatch");
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (rank == initiator) {
      rows.push_back(gloo::mpi_bench::buildRow(
          bytes,
          options.iterations,
          samples,
          static_cast<double>(bytes) * 2.0));
    }
  }

  if (rank == initiator) {
    std::cout << "\nmpi_bench_myelon_pingpong\n";
    std::cout << "  MPI-launched Myelon/disruptor SHM round-trip benchmark via C++ shim\n";
    std::cout << "  backend=myelon-shm, ranks=2, initiator=" << initiator
              << ", warmup=" << options.warmup
              << ", iterations=" << options.iterations
              << ", ring_depth=" << kRingDepth
              << ", max_payload=" << gloo::mpi_bench::humanBytes(max_payload) << "\n\n";
    gloo::mpi_bench::printBenchmarkTable(rows);
  }

  MPI_Barrier(MPI_COMM_WORLD);
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  bool mpi_initialized = false;
  try {
    MPI_Init(&argc, &argv);
    mpi_initialized = true;
    const int rc = run_benchmark(argc, argv);
    MPI_Finalize();
    return rc;
  } catch (const std::exception& error) {
    if (mpi_initialized) {
      int rank = -1;
      MPI_Comm_rank(MPI_COMM_WORLD, &rank);
      std::cerr << "Rank " << rank << " failed: " << error.what() << std::endl;
      MPI_Abort(MPI_COMM_WORLD, 1);
    } else {
      std::cerr << error.what() << std::endl;
    }
    return 1;
  }
}
