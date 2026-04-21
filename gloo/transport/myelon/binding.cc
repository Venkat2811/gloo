#include "gloo/transport/myelon/binding.h"

#include <utility>

#include "myelon_gloo_ffi.h"

namespace gloo {
namespace transport {
namespace myelon {

namespace {

[[noreturn]] void throwLastError(const std::string& action) {
  const char* message = myelon_gloo_last_error_message();
  throw Error(
      action + ": " + (message != nullptr ? std::string(message) : "unknown error"));
}

void checkStatus(int rc, const std::string& action) {
  if (rc != 0) {
    throwLastError(action);
  }
}

myelon_gloo_producer_t* asProducerHandle(void* handle) {
  return static_cast<myelon_gloo_producer_t*>(handle);
}

myelon_gloo_consumer_t* asConsumerHandle(void* handle) {
  return static_cast<myelon_gloo_consumer_t*>(handle);
}

} // namespace

Error::Error(const std::string& message) : std::runtime_error(message) {}

size_t maxPayloadBytes() {
  return myelon_gloo_max_payload_bytes();
}

size_t bucketPayloadBytes(size_t requested) {
  const auto bucket = myelon_gloo_bucket_payload_bytes(requested);
  if (bucket == 0) {
    throwLastError("select payload bucket");
  }
  return bucket;
}

Producer::Producer(
    const std::string& segmentName,
    size_t ringDepth,
    size_t maxPayloadBytes) {
  handle_ = myelon_gloo_producer_create(
      segmentName.c_str(), ringDepth, maxPayloadBytes);
  if (handle_ == nullptr) {
    throwLastError("create producer");
  }
}

Producer::~Producer() {
  myelon_gloo_producer_destroy(asProducerHandle(handle_));
}

Producer::Producer(Producer&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

Producer& Producer::operator=(Producer&& other) noexcept {
  if (this != &other) {
    myelon_gloo_producer_destroy(asProducerHandle(handle_));
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

void Producer::publish(const void* data, size_t len) {
  checkStatus(
      myelon_gloo_producer_publish(asProducerHandle(handle_), data, len),
      "publish payload");
}

Consumer::Consumer(
    const std::string& segmentName,
    size_t ringDepth,
    size_t maxPayloadBytes,
    const std::string& consumerId) {
  handle_ = myelon_gloo_consumer_attach(
      segmentName.c_str(),
      ringDepth,
      maxPayloadBytes,
      consumerId.c_str());
  if (handle_ == nullptr) {
    throwLastError("attach consumer");
  }
}

Consumer::~Consumer() {
  myelon_gloo_consumer_destroy(asConsumerHandle(handle_));
}

Consumer::Consumer(Consumer&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

Consumer& Consumer::operator=(Consumer&& other) noexcept {
  if (this != &other) {
    myelon_gloo_consumer_destroy(asConsumerHandle(handle_));
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

size_t Consumer::receiveBlocking(void* out, size_t outCapacity) {
  size_t outLen = 0;
  checkStatus(
      myelon_gloo_consumer_receive_blocking(
          asConsumerHandle(handle_), out, outCapacity, &outLen),
      "receive payload");
  return outLen;
}

} // namespace myelon
} // namespace transport
} // namespace gloo
