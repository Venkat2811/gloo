#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace gloo {
namespace transport {
namespace myelon {

class Error : public std::runtime_error {
 public:
  explicit Error(const std::string& message);
};

size_t maxPayloadBytes();
size_t bucketPayloadBytes(size_t requested);

class Producer {
 public:
  Producer(
      const std::string& segmentName,
      size_t ringDepth,
      size_t maxPayloadBytes);
  ~Producer();

  Producer(const Producer&) = delete;
  Producer& operator=(const Producer&) = delete;

  Producer(Producer&& other) noexcept;
  Producer& operator=(Producer&& other) noexcept;

  void publish(const void* data, size_t len);

 private:
  void* handle_{nullptr};
};

class Consumer {
 public:
  Consumer(
      const std::string& segmentName,
      size_t ringDepth,
      size_t maxPayloadBytes,
      const std::string& consumerId);
  ~Consumer();

  Consumer(const Consumer&) = delete;
  Consumer& operator=(const Consumer&) = delete;

  Consumer(Consumer&& other) noexcept;
  Consumer& operator=(Consumer&& other) noexcept;

  size_t receiveBlocking(void* out, size_t outCapacity);

 private:
  void* handle_{nullptr};
};

} // namespace myelon
} // namespace transport
} // namespace gloo
