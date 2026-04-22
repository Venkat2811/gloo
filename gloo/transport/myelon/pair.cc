#include "gloo/transport/myelon/pair.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <thread>

#include "gloo/common/logging.h"
#include "gloo/transport/myelon/binding.h"
#include "gloo/transport/myelon/context.h"
#include "gloo/transport/myelon/device.h"
#include "gloo/transport/myelon/unbound_buffer.h"

namespace gloo {
namespace transport {
namespace myelon {

namespace {

constexpr size_t kDefaultRingDepth = 64;
constexpr const char* kRingDepthEnvVar = "GLOO_MYELON_RING_DEPTH";

size_t effectiveSize(size_t nbytes) {
  return std::max<size_t>(1, nbytes);
}

size_t loadRingDepthFromEnv() {
  const char* value = std::getenv(kRingDepthEnvVar);
  if (value == nullptr || value[0] == '\0') {
    return kDefaultRingDepth;
  }

  char* end = nullptr;
  const auto parsed = std::strtoull(value, &end, 10);
  GLOO_ENFORCE(
      end != value && *end == '\0',
      "invalid value for ",
      kRingDepthEnvVar,
      ": ",
      value);
  GLOO_ENFORCE_GT(parsed, 0, kRingDepthEnvVar, " must be greater than zero");
  return static_cast<size_t>(parsed);
}

std::string encodeBase36(uint64_t value, size_t width) {
  static constexpr char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string out(width, '0');
  for (size_t i = 0; i < width; i++) {
    out[width - i - 1] = kAlphabet[value % 36];
    value /= 36;
  }
  return out;
}

uint64_t fnv1a64(const std::string& token, uint64_t tag, size_t bucketBytes) {
  uint64_t hash = 1469598103934665603ULL;
  const auto mixByte = [&](uint8_t byte) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  };

  for (const unsigned char ch : token) {
    mixByte(ch);
  }
  for (size_t i = 0; i < sizeof(tag); i++) {
    mixByte(static_cast<uint8_t>((tag >> (i * 8)) & 0xff));
  }
  for (size_t i = 0; i < sizeof(bucketBytes); i++) {
    mixByte(static_cast<uint8_t>((bucketBytes >> (i * 8)) & 0xff));
  }
  return hash;
}

char bucketCode(size_t bucketBytes) {
  switch (bucketBytes) {
    case 64:
      return 'a';
    case (size_t{1} << 10):
      return 'b';
    case (size_t{4} << 10):
      return 'c';
    case (size_t{64} << 10):
      return 'd';
    case (size_t{256} << 10):
      return 'e';
    case (size_t{1} << 20):
      return 'f';
    default:
      GLOO_THROW_INVALID_OPERATION_EXCEPTION("unknown Myelon bucket size ", bucketBytes);
  }
}

} // namespace

Pair::Pair(std::shared_ptr<Context> context, int rank, std::chrono::milliseconds timeout)
    : context_(std::move(context)),
      rank_(rank),
      timeout_(timeout),
      ringDepth_(loadRingDepthFromEnv()),
      localAddress_(context_->device()->nextAddress()),
      recvWorker_([this] { recvWorkerLoop(); }) {}

Pair::~Pair() {
  close();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopRecvWorker_ = true;
  }
  recvQueueCv_.notify_all();
  if (recvWorker_.joinable()) {
    recvWorker_.join();
  }
}

const Address& Pair::address() const {
  return localAddress_;
}

void Pair::connect(const std::vector<char>& bytes) {
  Address remote(bytes);
  GLOO_ENFORCE_EQ(
      remote.hostname(),
      localAddress_.hostname(),
      "Myelon transport only supports same-host peers");

  std::lock_guard<std::mutex> lock(mutex_);
  remoteAddress_ = remote;
  producers_.clear();
  consumers_.clear();
  connected_ = true;
}

void Pair::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  recvQueue_.clear();
  consumers_.clear();
  producers_.clear();
  connected_ = false;
}

void Pair::setSync(bool /*enable*/, bool /*busyPoll*/) {}

std::unique_ptr<::gloo::transport::Buffer> Pair::createSendBuffer(int, void*, size_t) {
  GLOO_THROW_INVALID_OPERATION_EXCEPTION(
      "Myelon transport does not implement bound send buffers");
}

std::unique_ptr<::gloo::transport::Buffer> Pair::createRecvBuffer(int, void*, size_t) {
  GLOO_THROW_INVALID_OPERATION_EXCEPTION(
      "Myelon transport does not implement bound recv buffers");
}

bool Pair::isConnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

void Pair::send(
    ::gloo::transport::UnboundBuffer* buf,
    uint64_t tag,
    size_t offset,
    size_t nbytes) {
  ensureConnected();
  GLOO_ENFORCE_LE(offset + nbytes, buf->size, "send range exceeds buffer size");
  const auto bucket = bucketPayloadBytes(effectiveSize(nbytes));
  auto* ptr = static_cast<uint8_t*>(buf->ptr) + offset;
  getOrCreateProducer(tag, bucket).publish(ptr, nbytes);
  static_cast<myelon::UnboundBuffer*>(buf)->handleSendCompletion(rank_);
}

void Pair::recv(
    ::gloo::transport::UnboundBuffer* buf,
    uint64_t tag,
    size_t offset,
    size_t nbytes) {
  ensureConnected();
  GLOO_ENFORCE_LE(offset + nbytes, buf->size, "recv range exceeds buffer size");
  const auto bucket = bucketPayloadBytes(effectiveSize(nbytes));
  auto* myelonBuf = static_cast<myelon::UnboundBuffer*>(buf);
  auto* ptr = static_cast<uint8_t*>(buf->ptr) + offset;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    recvQueue_.push_back(RecvRequest{myelonBuf, tag, bucket, nbytes, ptr});
  }
  recvQueueCv_.notify_one();
}

std::string Pair::makeSegmentName(
    const std::string& token,
    uint64_t tag,
    size_t bucketBytes) {
  return "m" + std::string(1, bucketCode(bucketBytes)) +
      encodeBase36(fnv1a64(token, tag, bucketBytes), 10);
}

std::string Pair::makeConsumerId(int selfRank, int peerRank, size_t bucketBytes) {
  (void)peerRank;
  (void)bucketBytes;
  return "c" + std::to_string(selfRank);
}

Producer& Pair::getOrCreateProducer(uint64_t tag, size_t bucketBytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  const ChannelKey key{tag, bucketBytes};
  auto it = producers_.find(key);
  if (it == producers_.end()) {
    it = producers_
             .emplace(
                 key,
                 std::make_unique<Producer>(
                     makeSegmentName(localAddress_.token(), tag, bucketBytes),
                     ringDepth(),
                     bucketBytes))
             .first;
  }
  return *it->second;
}

Consumer& Pair::getOrCreateConsumer(uint64_t tag, size_t bucketBytes) {
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  size_t spinCount = 0;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      const ChannelKey key{tag, bucketBytes};
      auto it = consumers_.find(key);
      if (it != consumers_.end()) {
        return *it->second;
      }
      try {
        it = consumers_
                 .emplace(
                     key,
                     std::make_unique<Consumer>(
                         makeSegmentName(remoteAddress_.token(), tag, bucketBytes),
                         ringDepth(),
                         bucketBytes,
                         makeConsumerId(context_->rank, rank_, bucketBytes)))
                 .first;
        return *it->second;
      } catch (const Error&) {
      }
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      GLOO_THROW_IO_EXCEPTION(
          "timed out attaching Myelon consumer for bucket ", bucketBytes);
    }
    if (spinCount < 1000) {
      ++spinCount;
      std::this_thread::yield();
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  }
}

void Pair::ensureConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  GLOO_ENFORCE(connected_, "pair must be connected before use");
}

size_t Pair::ringDepth() const {
  return ringDepth_;
}

void Pair::recvWorkerLoop() {
  for (;;) {
    RecvRequest request{};
    {
      std::unique_lock<std::mutex> lock(mutex_);
      recvQueueCv_.wait(lock, [&] { return stopRecvWorker_ || !recvQueue_.empty(); });
      if (stopRecvWorker_ && recvQueue_.empty()) {
        return;
      }
      request = recvQueue_.front();
      recvQueue_.pop_front();
    }

    try {
      const auto received =
          getOrCreateConsumer(request.tag, request.bucketBytes)
              .receiveBlocking(request.ptr, request.nbytes);
      GLOO_ENFORCE_EQ(received, request.nbytes, "received byte count mismatch");
      request.myelonBuf->handleRecvCompletion(rank_);
    } catch (...) {
      request.myelonBuf->handleRecvError(std::current_exception());
    }
  }
}

} // namespace myelon
} // namespace transport
} // namespace gloo
