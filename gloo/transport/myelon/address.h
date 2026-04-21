#pragma once

#include <array>
#include <string>
#include <vector>

#include "gloo/transport/address.h"

namespace gloo {
namespace transport {
namespace myelon {

class Address final : public ::gloo::transport::Address {
 public:
  static constexpr size_t kHostnameBytes = 64;
  static constexpr size_t kTokenBytes = 64;

  Address() = default;
  Address(const std::string& hostname, const std::string& token);
  explicit Address(const std::vector<char>& bytes);

  std::string str() const override;
  std::vector<char> bytes() const override;

  const std::string& hostname() const {
    return hostname_;
  }

  const std::string& token() const {
    return token_;
  }

 private:
  std::string hostname_;
  std::string token_;
};

} // namespace myelon
} // namespace transport
} // namespace gloo
