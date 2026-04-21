#pragma once

#include <memory>

#include "gloo/transport/context.h"

namespace gloo {
namespace transport {
namespace myelon {

class Device;
class Pair;
class UnboundBuffer;

class Context final : public ::gloo::transport::Context,
                      public std::enable_shared_from_this<Context> {
 public:
  Context(std::shared_ptr<Device> device, int rank, int size);
  ~Context() override;

  std::unique_ptr<transport::Pair>& createPair(int rank) override;
  std::unique_ptr<transport::UnboundBuffer> createUnboundBuffer(
      void* ptr,
      size_t size) override;

  std::shared_ptr<Device> device() const {
    return device_;
  }

 private:
  std::shared_ptr<Device> device_;
};

} // namespace myelon
} // namespace transport
} // namespace gloo
