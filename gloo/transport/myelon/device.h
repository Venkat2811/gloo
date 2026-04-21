#pragma once

#include <memory>
#include <string>

#include "gloo/transport/device.h"
#include "gloo/transport/myelon/address.h"

namespace gloo {
namespace transport {
namespace myelon {

struct attr {
  std::string hostname;
};

class Device;

std::shared_ptr<::gloo::transport::Device> CreateDevice(const struct attr& attr = {});

class Device final : public ::gloo::transport::Device,
                     public std::enable_shared_from_this<Device> {
 public:
  explicit Device(const struct attr& attr);

  std::string str() const override;
  const std::string& getPCIBusID() const override;
  std::shared_ptr<::gloo::transport::Context> createContext(int rank, int size) override;

  Address nextAddress();

 private:
  const std::string hostname_;
  const std::string deviceTokenPrefix_;
  const std::string pciBusId_;
  uint64_t nextAddressSequence_{0};
};

} // namespace myelon
} // namespace transport
} // namespace gloo
