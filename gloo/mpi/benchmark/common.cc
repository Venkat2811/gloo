#include "gloo/mpi/benchmark/common.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

#include "gloo/config.h"
#include "gloo/mpi/context.h"

#if GLOO_HAVE_TRANSPORT_TCP
#include "gloo/transport/tcp/device.h"
#endif

#if GLOO_HAVE_TRANSPORT_UV
#include "gloo/transport/uv/device.h"
#endif

namespace gloo {
namespace mpi_bench {
namespace {

[[noreturn]] void failUsage(const char* benchName, const std::string& message) {
  std::ostringstream oss;
  oss << "Usage: " << benchName
      << " [--transport=auto|tcp|uv] [--iface=IFACE] [--warmup=N] "
         "[--iterations=N] [--root=R] [--sizes=4,64,1KiB,...] "
         "[--min-bytes=N] [--max-bytes=N] [--factor=N] [--no-verify]\n"
      << "Example: " << benchName
      << " --transport=auto --sizes=4,64,1KiB,16KiB,256KiB,1MiB\n";
  if (!message.empty()) {
    oss << "\nError: " << message;
  }
  throw std::invalid_argument(oss.str());
}

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string trim(std::string value) {
  const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
  value.erase(
      value.begin(),
      std::find_if(value.begin(), value.end(), notSpace));
  value.erase(
      std::find_if(value.rbegin(), value.rend(), notSpace).base(),
      value.end());
  return value;
}

int parseIntFlag(const std::string& value, const char* name) {
  try {
    size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size()) {
      throw std::invalid_argument("trailing characters");
    }
    return result;
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string("invalid ") + name + ": " + value);
  }
}

size_t parseSizeFlag(const std::string& value, const char* name) {
  try {
    return parseByteSize(value);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string("invalid ") + name + ": " + value);
  }
}

std::vector<std::string> split(const std::string& value, char delim) {
  std::vector<std::string> parts;
  std::stringstream ss(value);
  std::string part;
  while (std::getline(ss, part, delim)) {
    part = trim(part);
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  return parts;
}

std::vector<size_t> buildSizeSweep(const BenchOptions& options) {
  if (!options.sizes.empty()) {
    return options.sizes;
  }

  if (options.minBytes == 0 || options.maxBytes == 0) {
    throw std::invalid_argument("min-bytes and max-bytes must be positive");
  }
  if (options.minBytes > options.maxBytes) {
    throw std::invalid_argument("min-bytes must be <= max-bytes");
  }
  if (options.factor < 2) {
    throw std::invalid_argument("factor must be >= 2");
  }

  std::vector<size_t> sizes;
  for (size_t bytes = options.minBytes; bytes <= options.maxBytes;) {
    sizes.push_back(bytes);
    if (bytes > std::numeric_limits<size_t>::max() / options.factor) {
      break;
    }
    const auto next = bytes * options.factor;
    if (next == bytes) {
      break;
    }
    bytes = next;
  }

  if (sizes.back() != options.maxBytes) {
    sizes.push_back(options.maxBytes);
  }
  return sizes;
}

std::shared_ptr<transport::Device> createDevice(const BenchOptions& options) {
#if GLOO_HAVE_TRANSPORT_TCP
  if (options.transport == "auto" || options.transport == "tcp") {
    transport::tcp::attr attr;
    if (!options.iface.empty()) {
      attr.iface = options.iface;
    }
    return transport::tcp::CreateDevice(attr);
  }
#endif

#if GLOO_HAVE_TRANSPORT_UV
  if (options.transport == "auto" || options.transport == "uv") {
    transport::uv::attr attr;
    if (!options.iface.empty()) {
      attr.iface = options.iface;
    }
    return transport::uv::CreateDevice(attr);
  }
#endif

  throw std::invalid_argument("requested transport is not available in this build");
}

long long percentile(const std::vector<long long>& samples, double pct) {
  if (samples.empty()) {
    return 0;
  }
  const auto bounded = std::clamp(pct, 0.0, 1.0);
  const auto index = static_cast<size_t>(
      std::llround(bounded * static_cast<double>(samples.size() - 1)));
  return samples[index];
}

std::string formatDouble(double value, int precision) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(precision) << value;
  return oss.str();
}

} // namespace

BenchOptions parseBenchOptions(int argc, char** argv, const char* benchName) {
  BenchOptions options;

  for (int i = 1; i < argc; i++) {
    const std::string arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      failUsage(benchName, "");
    }
    if (arg == "--no-verify") {
      options.verify = false;
      continue;
    }
    if (startsWith(arg, "--transport=")) {
      options.transport = arg.substr(std::string("--transport=").size());
      continue;
    }
    if (startsWith(arg, "--iface=")) {
      options.iface = arg.substr(std::string("--iface=").size());
      continue;
    }
    if (startsWith(arg, "--warmup=")) {
      options.warmup =
          parseIntFlag(arg.substr(std::string("--warmup=").size()), "warmup");
      continue;
    }
    if (startsWith(arg, "--iterations=")) {
      options.iterations = parseIntFlag(
          arg.substr(std::string("--iterations=").size()), "iterations");
      continue;
    }
    if (startsWith(arg, "--root=")) {
      options.root = parseIntFlag(arg.substr(std::string("--root=").size()), "root");
      continue;
    }
    if (startsWith(arg, "--sizes=")) {
      options.sizes.clear();
      for (const auto& part :
           split(arg.substr(std::string("--sizes=").size()), ',')) {
        options.sizes.push_back(parseSizeFlag(part, "sizes"));
      }
      continue;
    }
    if (startsWith(arg, "--min-bytes=")) {
      options.minBytes =
          parseSizeFlag(arg.substr(std::string("--min-bytes=").size()), "min-bytes");
      continue;
    }
    if (startsWith(arg, "--max-bytes=")) {
      options.maxBytes =
          parseSizeFlag(arg.substr(std::string("--max-bytes=").size()), "max-bytes");
      continue;
    }
    if (startsWith(arg, "--factor=")) {
      options.factor =
          parseSizeFlag(arg.substr(std::string("--factor=").size()), "factor");
      continue;
    }
    failUsage(benchName, "unknown argument: " + arg);
  }

  if (options.warmup < 0) {
    failUsage(benchName, "warmup must be >= 0");
  }
  if (options.iterations <= 0) {
    failUsage(benchName, "iterations must be > 0");
  }
  if (options.root < 0) {
    failUsage(benchName, "root must be >= 0");
  }
  if (options.transport != "auto" && options.transport != "tcp" &&
      options.transport != "uv") {
    failUsage(benchName, "transport must be auto, tcp, or uv");
  }

  try {
    options.sizes = buildSizeSweep(options);
  } catch (const std::exception& error) {
    failUsage(benchName, error.what());
  }

  return options;
}

BenchmarkSession createBenchmarkSession(const BenchOptions& options) {
  BenchmarkSession session;
  MPI_Comm_rank(MPI_COMM_WORLD, &session.rank);
  MPI_Comm_size(MPI_COMM_WORLD, &session.size);
  if (options.root >= session.size) {
    throw std::invalid_argument("root rank is outside MPI_COMM_WORLD");
  }

  session.device = createDevice(options);
  auto context = std::make_shared<::gloo::mpi::Context>(MPI_COMM_WORLD);
  context->setTimeout(std::chrono::seconds(30));
  context->connectFullMesh(session.device);
  session.context = context;
  return session;
}

std::vector<long long> reduceMaxSamples(const std::vector<long long>& localSamples) {
  std::vector<long long> reduced(localSamples.size(), 0);
  MPI_Reduce(
      localSamples.data(),
      reduced.data(),
      static_cast<int>(localSamples.size()),
      MPI_LONG_LONG,
      MPI_MAX,
      0,
      MPI_COMM_WORLD);
  return reduced;
}

BenchRow buildRow(
    size_t bytes,
    int iterations,
    const std::vector<long long>& samples,
    double payloadBytesPerOp) {
  if (samples.empty()) {
    throw std::invalid_argument("cannot build a row from zero samples");
  }

  std::vector<long long> sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  const double totalNs = std::accumulate(
      sorted.begin(), sorted.end(), 0.0, [](double acc, long long value) {
        return acc + static_cast<double>(value);
      });
  const double meanNs = totalNs / static_cast<double>(sorted.size());
  const double payloadGiBPerSec =
      payloadBytesPerOp / (meanNs / 1e9) / static_cast<double>(1ull << 30);

  BenchRow row;
  row.bytes = bytes;
  row.iterations = iterations;
  row.minNs = sorted.front();
  row.p50Ns = percentile(sorted, 0.50);
  row.p95Ns = percentile(sorted, 0.95);
  row.p99Ns = percentile(sorted, 0.99);
  row.maxNs = sorted.back();
  row.meanNs = meanNs;
  row.payloadGiBPerSec = payloadGiBPerSec;
  return row;
}

void printBenchmarkHeader(
    const char* benchName,
    const std::string& description,
    const BenchOptions& options,
    const BenchmarkSession& session) {
  if (session.rank != 0) {
    return;
  }

  std::cout << "\n" << benchName << "\n";
  std::cout << "  " << description << "\n";
  std::cout << "  transport=" << options.transport;
  if (!options.iface.empty()) {
    std::cout << ", iface=" << options.iface;
  }
  std::cout << ", ranks=" << session.size << ", warmup=" << options.warmup
            << ", iterations=" << options.iterations;
  if (options.root >= 0) {
    std::cout << ", root=" << options.root;
  }
  std::cout << "\n";
  std::cout << "  device=" << session.device->str() << "\n\n";
}

void printBenchmarkTable(const std::vector<BenchRow>& rows) {
  if (rows.empty()) {
    return;
  }

  constexpr int kBytesWidth = 11;
  constexpr int kIterWidth = 10;
  constexpr int kLatencyWidth = 11;
  constexpr int kBwWidth = 15;

  auto printRule = [&]() {
    std::cout << "+" << std::string(kBytesWidth + 2, '-')
              << "+" << std::string(kIterWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kLatencyWidth + 2, '-')
              << "+" << std::string(kBwWidth + 2, '-') << "+\n";
  };

  auto printCell = [](const std::string& value, int width, bool rightAlign = true) {
    std::cout << " " << (rightAlign ? std::right : std::left) << std::setw(width)
              << value << " ";
  };

  printRule();
  std::cout << "|";
  printCell("Bytes", kBytesWidth, false);
  std::cout << "|";
  printCell("Iters", kIterWidth, false);
  std::cout << "|";
  printCell("Min us", kLatencyWidth, false);
  std::cout << "|";
  printCell("P50 us", kLatencyWidth, false);
  std::cout << "|";
  printCell("P95 us", kLatencyWidth, false);
  std::cout << "|";
  printCell("P99 us", kLatencyWidth, false);
  std::cout << "|";
  printCell("Max us", kLatencyWidth, false);
  std::cout << "|";
  printCell("Mean us", kLatencyWidth, false);
  std::cout << "|";
  printCell("Payload GiB/s", kBwWidth, false);
  std::cout << "|\n";
  printRule();

  for (const auto& row : rows) {
    std::cout << "|";
    printCell(humanBytes(row.bytes), kBytesWidth);
    std::cout << "|";
    printCell(std::to_string(row.iterations), kIterWidth);
    std::cout << "|";
    printCell(formatDouble(row.minNs / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.p50Ns / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.p95Ns / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.p99Ns / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.maxNs / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.meanNs / 1000.0, 2), kLatencyWidth);
    std::cout << "|";
    printCell(formatDouble(row.payloadGiBPerSec, 3), kBwWidth);
    std::cout << "|\n";
  }

  printRule();
}

size_t parseByteSize(const std::string& input) {
  const auto value = trim(input);
  if (value.empty()) {
    throw std::invalid_argument("empty size");
  }

  size_t splitIndex = 0;
  while (splitIndex < value.size() &&
         (std::isdigit(static_cast<unsigned char>(value[splitIndex])) ||
          value[splitIndex] == '.')) {
    splitIndex++;
  }

  if (splitIndex == 0) {
    throw std::invalid_argument("missing numeric prefix");
  }

  const double number = std::stod(value.substr(0, splitIndex));
  std::string suffix = value.substr(splitIndex);
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  size_t multiplier = 1;
  if (suffix.empty() || suffix == "b") {
    multiplier = 1;
  } else if (suffix == "k" || suffix == "kb" || suffix == "kib") {
    multiplier = 1ull << 10;
  } else if (suffix == "m" || suffix == "mb" || suffix == "mib") {
    multiplier = 1ull << 20;
  } else if (suffix == "g" || suffix == "gb" || suffix == "gib") {
    multiplier = 1ull << 30;
  } else {
    throw std::invalid_argument("unsupported size suffix");
  }

  return static_cast<size_t>(std::llround(number * multiplier));
}

std::string humanBytes(size_t bytes) {
  constexpr double kKiB = 1024.0;
  constexpr double kMiB = 1024.0 * 1024.0;
  constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;

  std::ostringstream oss;
  if (bytes >= (1ull << 30)) {
    oss << std::fixed << std::setprecision(2) << (bytes / kGiB) << " GiB";
  } else if (bytes >= (1ull << 20)) {
    oss << std::fixed << std::setprecision(2) << (bytes / kMiB) << " MiB";
  } else if (bytes >= (1ull << 10)) {
    oss << std::fixed << std::setprecision(2) << (bytes / kKiB) << " KiB";
  } else {
    oss << bytes << " B";
  }
  return oss.str();
}

int runBenchmarkMain(
    int argc,
    char** argv,
    const char* benchName,
    const std::string& description,
    const RowBuilder& rowBuilder) {
  bool mpiInitialized = false;
  try {
    const auto options = parseBenchOptions(argc, argv, benchName);
    MPI_Init(&argc, &argv);
    mpiInitialized = true;

    {
      const auto session = createBenchmarkSession(options);
      const auto rows = rowBuilder(options, session);
      printBenchmarkHeader(benchName, description, options, session);
      printBenchmarkTable(rows);
      MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
  } catch (const std::exception& error) {
    if (mpiInitialized) {
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

} // namespace mpi_bench
} // namespace gloo
