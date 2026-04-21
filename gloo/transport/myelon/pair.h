#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "gloo/transport/myelon/address.h"
#include "gloo/transport/pair.h"

namespace gloo {
namespace transport {
namespace myelon {

class Consumer;
class Context;
class Producer;

class Pair final : public ::gloo::transport::Pair {
 public:
  Pair(std::shared_ptr<Context> context, int rank, std::chrono::milliseconds timeout);
  ~Pair() override;

  const Address& address() const override;
  void connect(const std::vector<char>& bytes) override;
  void close() override;
  void setSync(bool enable, bool busyPoll) override;
  std::unique_ptr<::gloo::transport::Buffer> createSendBuffer(
      int slot,
      void* ptr,
      size_t size) override;
  std::unique_ptr<::gloo::transport::Buffer> createRecvBuffer(
      int slot,
      void* ptr,
      size_t size) override;
  bool isConnected() override;
  void send(
      ::gloo::transport::UnboundBuffer* buf,
      uint64_t tag,
      size_t offset,
      size_t nbytes) override;
  void recv(
      ::gloo::transport::UnboundBuffer* buf,
      uint64_t tag,
      size_t offset,
      size_t nbytes) override;

 private:
  struct ChannelKey {
    uint64_t tag;
    size_t bucketBytes;

    bool operator==(const ChannelKey& other) const {
      return tag == other.tag && bucketBytes == other.bucketBytes;
    }
  };

  struct ChannelKeyHash {
    size_t operator()(const ChannelKey& key) const {
      return std::hash<uint64_t>{}(key.tag) ^
          (std::hash<size_t>{}(key.bucketBytes) << 1);
    }
  };

  static std::string makeSegmentName(
      const std::string& token,
      uint64_t tag,
      size_t bucketBytes);
  static std::string makeConsumerId(int selfRank, int peerRank, size_t bucketBytes);

  struct RecvBucketState {
    std::mutex mutex;
    std::condition_variable cv;
    uint64_t nextSequenceToAssign{0};
    uint64_t nextSequenceToRun{0};
  };

  Producer& getOrCreateProducer(uint64_t tag, size_t bucketBytes);
  Consumer& getOrCreateConsumer(uint64_t tag, size_t bucketBytes);
  std::shared_ptr<RecvBucketState> getOrCreateRecvBucketState(
      uint64_t tag,
      size_t bucketBytes);
  void ensureConnected() const;

  std::shared_ptr<Context> context_;
  const int rank_;
  const std::chrono::milliseconds timeout_;
  Address localAddress_;
  Address remoteAddress_;
  bool connected_{false};
  mutable std::mutex mutex_;
  std::unordered_map<ChannelKey, std::unique_ptr<Producer>, ChannelKeyHash> producers_;
  std::unordered_map<ChannelKey, std::unique_ptr<Consumer>, ChannelKeyHash> consumers_;
  std::unordered_map<ChannelKey, std::shared_ptr<RecvBucketState>, ChannelKeyHash>
      recvBucketStates_;
};

} // namespace myelon
} // namespace transport
} // namespace gloo
