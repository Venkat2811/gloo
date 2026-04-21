#include "gloo/transport/myelon/context.h"

#include "gloo/transport/myelon/device.h"
#include "gloo/transport/myelon/pair.h"
#include "gloo/transport/myelon/unbound_buffer.h"

namespace gloo {
namespace transport {
namespace myelon {

Context::Context(std::shared_ptr<Device> device, int rank, int size)
    : ::gloo::transport::Context(rank, size), device_(std::move(device)) {}

Context::~Context() {
  pairs_.clear();
  device_.reset();
}

std::unique_ptr<transport::Pair>& Context::createPair(int rank) {
  pairs_[rank] = std::unique_ptr<transport::Pair>(
      new myelon::Pair(shared_from_this(), rank, getTimeout()));
  return pairs_[rank];
}

std::unique_ptr<transport::UnboundBuffer> Context::createUnboundBuffer(
    void* ptr,
    size_t size) {
  auto* buf = new myelon::UnboundBuffer(shared_from_this(), ptr, size);
  return std::unique_ptr<transport::UnboundBuffer>(buf);
}

} // namespace myelon
} // namespace transport
} // namespace gloo
