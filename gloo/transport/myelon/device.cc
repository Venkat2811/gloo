#include "gloo/transport/myelon/device.h"

#include <chrono>
#include <iomanip>
#include <memory>
#include <sstream>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "gloo/common/utils.h"
#include "gloo/transport/myelon/context.h"

namespace gloo {
namespace transport {
namespace myelon {

namespace {

std::string makeHexToken(uint64_t value, int width) {
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << std::setw(width) << std::setfill('0') << value;
  return oss.str();
}

} // namespace

std::shared_ptr<::gloo::transport::Device> CreateDevice(const struct attr& attr) {
  return std::make_shared<Device>(attr);
}

Device::Device(const struct attr& attr)
    : hostname_(attr.hostname.empty() ? ::gloo::getHostname() : attr.hostname),
      deviceTokenPrefix_([&] {
        const auto now = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
#ifdef _WIN32
        const auto pid = static_cast<uint64_t>(0);
#else
        const auto pid = static_cast<uint64_t>(::getpid());
#endif
        return makeHexToken(now ^ (pid << 17), 4);
      }()),
      pciBusId_(""),
      nextAddressSequence_(0) {}

std::string Device::str() const {
  return "myelon-shm://" + hostname_ + "/" + deviceTokenPrefix_;
}

const std::string& Device::getPCIBusID() const {
  return pciBusId_;
}

std::shared_ptr<::gloo::transport::Context> Device::createContext(int rank, int size) {
  return std::make_shared<Context>(shared_from_this(), rank, size);
}

Address Device::nextAddress() {
  std::ostringstream oss;
  oss << deviceTokenPrefix_ << makeHexToken(nextAddressSequence_++, 3);
  return Address(hostname_, oss.str());
}

} // namespace myelon
} // namespace transport
} // namespace gloo
