#pragma once

#include <mpi.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gloo/context.h"
#include "gloo/transport/device.h"

namespace gloo {
namespace mpi_bench {

struct BenchOptions {
  std::string transport = "auto";
  std::string iface;
  int warmup = 25;
  int iterations = 200;
  int root = 0;
  bool verify = true;
  std::vector<size_t> sizes;
  size_t minBytes = 4;
  size_t maxBytes = 1 << 20;
  size_t factor = 4;
};

struct BenchmarkSession {
  std::shared_ptr<transport::Device> device;
  std::shared_ptr<::gloo::Context> context;
  int rank = 0;
  int size = 0;
};

struct BenchRow {
  size_t bytes = 0;
  int iterations = 0;
  long long minNs = 0;
  long long p50Ns = 0;
  long long p95Ns = 0;
  long long p99Ns = 0;
  long long maxNs = 0;
  double meanNs = 0.0;
  double payloadGiBPerSec = 0.0;
};

using RowBuilder =
    std::function<std::vector<BenchRow>(const BenchOptions&, const BenchmarkSession&)>;

BenchOptions parseBenchOptions(int argc, char** argv, const char* benchName);
BenchmarkSession createBenchmarkSession(const BenchOptions& options);

std::vector<long long> reduceMaxSamples(const std::vector<long long>& localSamples);
BenchRow buildRow(
    size_t bytes,
    int iterations,
    const std::vector<long long>& samples,
    double payloadBytesPerOp);
void printBenchmarkHeader(
    const char* benchName,
    const std::string& description,
    const BenchOptions& options,
    const BenchmarkSession& session);
void printBenchmarkTable(const std::vector<BenchRow>& rows);

size_t parseByteSize(const std::string& input);
std::string humanBytes(size_t bytes);

int runBenchmarkMain(
    int argc,
    char** argv,
    const char* benchName,
    const std::string& description,
    const RowBuilder& rowBuilder);

} // namespace mpi_bench
} // namespace gloo
