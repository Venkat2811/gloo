#pragma once

#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>

#include "gloo/common/memory.h"
#include "gloo/transport/unbound_buffer.h"

namespace gloo {
namespace transport {
namespace myelon {

class Context;
class Pair;

class UnboundBuffer final : public ::gloo::transport::UnboundBuffer {
 public:
  UnboundBuffer(std::shared_ptr<Context> context, void* ptr, size_t size);
  ~UnboundBuffer() override;

  bool waitRecv(int* rank, std::chrono::milliseconds timeout) override;
  bool waitSend(int* rank, std::chrono::milliseconds timeout) override;
  void abortWaitRecv() override;
  void abortWaitSend() override;

  void send(int dstRank, uint64_t tag, size_t offset, size_t nbytes) override;
  void recv(int srcRank, uint64_t tag, size_t offset, size_t nbytes) override;
  void recv(std::vector<int> srcRanks, uint64_t tag, size_t offset, size_t nbytes) override;

 private:
  void handleRecvCompletion(int rank);
  void handleSendCompletion(int rank);
  void handleRecvError(std::exception_ptr error);
  void handleSendError(std::exception_ptr error);

  std::shared_ptr<Context> context_;
  std::mutex mutex_;
  std::condition_variable recvCv_;
  std::condition_variable sendCv_;
  bool abortWaitRecv_{false};
  bool abortWaitSend_{false};
  int recvCompletions_{0};
  int recvRank_{-1};
  int sendCompletions_{0};
  int sendRank_{-1};
  std::exception_ptr recvException_;
  std::exception_ptr sendException_;

  friend class Pair;
};

} // namespace myelon
} // namespace transport
} // namespace gloo
