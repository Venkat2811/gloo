#include "gloo/transport/myelon/address.h"

#include <cstring>

#include "gloo/common/logging.h"

namespace gloo {
namespace transport {
namespace myelon {

namespace {

struct SerializedAddress {
  char hostname[Address::kHostnameBytes];
  char token[Address::kTokenBytes];
};

static_assert(std::is_trivially_copyable<SerializedAddress>::value, "!");

std::string readField(const char* data, size_t size) {
  const auto len = strnlen(data, size);
  return std::string(data, len);
}

void writeField(const std::string& value, char* out, size_t size, const char* name) {
  GLOO_ENFORCE_LT(value.size(), size, name, " is too long for serialized address");
  std::memset(out, 0, size);
  std::memcpy(out, value.data(), value.size());
}

} // namespace

Address::Address(const std::string& hostname, const std::string& token)
    : hostname_(hostname), token_(token) {
  GLOO_ENFORCE(!hostname_.empty(), "hostname must not be empty");
  GLOO_ENFORCE(!token_.empty(), "token must not be empty");
}

Address::Address(const std::vector<char>& bytes) {
  GLOO_ENFORCE_EQ(
      bytes.size(),
      sizeof(SerializedAddress),
      "invalid Myelon address byte length");
  SerializedAddress storage{};
  std::memcpy(&storage, bytes.data(), sizeof(storage));
  hostname_ = readField(storage.hostname, sizeof(storage.hostname));
  token_ = readField(storage.token, sizeof(storage.token));
  GLOO_ENFORCE(!hostname_.empty(), "serialized hostname must not be empty");
  GLOO_ENFORCE(!token_.empty(), "serialized token must not be empty");
}

std::string Address::str() const {
  return "myelon://" + hostname_ + "/" + token_;
}

std::vector<char> Address::bytes() const {
  SerializedAddress storage{};
  writeField(hostname_, storage.hostname, sizeof(storage.hostname), "hostname");
  writeField(token_, storage.token, sizeof(storage.token), "token");
  const auto* ptr = reinterpret_cast<const char*>(&storage);
  return std::vector<char>(ptr, ptr + sizeof(storage));
}

} // namespace myelon
} // namespace transport
} // namespace gloo
