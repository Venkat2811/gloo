#include "gloo/transport/myelon/pair.h"

#include <algorithm>
#include <array>
#include <chrono>
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

constexpr std::array<size_t, 6> kBucketSizes = {
    64,
    size_t{1} << 10,
    size_t{4} << 10,
    size_t{64} << 10,
    size_t{256} << 10,
    size_t{1} << 20,
};

size_t effectiveSize(size_t nbytes) {
  return std::max<size_t>(1, nbytes);
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
      localAddress_(context_->device()->nextAddress()) {}

Pair::~Pair() = default;

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
  recvBucketStates_.clear();
  connected_ = true;
}

void Pair::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  consumers_.clear();
  producers_.clear();
  recvBucketStates_.clear();
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
    uint64_t /*tag*/,
    size_t offset,
    size_t nbytes) {
  ensureConnected();
  GLOO_ENFORCE_LE(offset + nbytes, buf->size, "send range exceeds buffer size");
  const auto bucket = bucketPayloadBytes(effectiveSize(nbytes));
  auto* ptr = static_cast<uint8_t*>(buf->ptr) + offset;
  getOrCreateProducer(bucket).publish(ptr, nbytes);
  static_cast<myelon::UnboundBuffer*>(buf)->handleSendCompletion(rank_);
}

void Pair::recv(
    ::gloo::transport::UnboundBuffer* buf,
    uint64_t /*tag*/,
    size_t offset,
    size_t nbytes) {
  ensureConnected();
  GLOO_ENFORCE_LE(offset + nbytes, buf->size, "recv range exceeds buffer size");
  const auto bucket = bucketPayloadBytes(effectiveSize(nbytes));
  (void)getOrCreateProducer(bucket);
  auto* myelonBuf = static_cast<myelon::UnboundBuffer*>(buf);
  auto* ptr = static_cast<uint8_t*>(buf->ptr) + offset;
  auto state = getOrCreateRecvBucketState(bucket);
  uint64_t sequence = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    sequence = state->nextSequenceToAssign++;
  }
  std::thread(
      [this, bucket, nbytes, ptr, myelonBuf, state, sequence] {
        try {
          {
            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [&] { return state->nextSequenceToRun == sequence; });
          }
          const auto received = getOrCreateConsumer(bucket).receiveBlocking(ptr, nbytes);
          GLOO_ENFORCE_EQ(received, nbytes, "received byte count mismatch");
          myelonBuf->handleRecvCompletion(rank_);
        } catch (...) {
          myelonBuf->handleRecvError(std::current_exception());
        }
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          state->nextSequenceToRun++;
        }
        state->cv.notify_all();
      })
      .detach();
}

std::string Pair::makeSegmentName(const std::string& token, size_t bucketBytes) {
  return token + std::string(1, bucketCode(bucketBytes));
}

std::string Pair::makeConsumerId(int selfRank, int peerRank, size_t bucketBytes) {
  (void)peerRank;
  (void)bucketBytes;
  return "c" + std::to_string(selfRank);
}

Producer& Pair::getOrCreateProducer(size_t bucketBytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = producers_.find(bucketBytes);
  if (it == producers_.end()) {
    it = producers_
             .emplace(
                 bucketBytes,
                 std::make_unique<Producer>(
                     makeSegmentName(localAddress_.token(), bucketBytes),
                     64,
                     bucketBytes))
             .first;
  }
  return *it->second;
}

Consumer& Pair::getOrCreateConsumer(size_t bucketBytes) {
  const auto deadline = std::chrono::steady_clock::now() + timeout_;
  for (;;) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = consumers_.find(bucketBytes);
      if (it != consumers_.end()) {
        return *it->second;
      }
      try {
        it = consumers_
                 .emplace(
                     bucketBytes,
                     std::make_unique<Consumer>(
                         makeSegmentName(remoteAddress_.token(), bucketBytes),
                         64,
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
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

std::shared_ptr<Pair::RecvBucketState> Pair::getOrCreateRecvBucketState(size_t bucketBytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = recvBucketStates_.find(bucketBytes);
  if (it == recvBucketStates_.end()) {
    it = recvBucketStates_.emplace(bucketBytes, std::make_shared<RecvBucketState>()).first;
  }
  return it->second;
}

void Pair::ensureConnected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  GLOO_ENFORCE(connected_, "pair must be connected before use");
}

} // namespace myelon
} // namespace transport
} // namespace gloo
