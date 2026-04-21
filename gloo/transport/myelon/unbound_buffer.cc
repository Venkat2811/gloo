#include "gloo/transport/myelon/unbound_buffer.h"

#include "gloo/common/error.h"
#include "gloo/common/logging.h"
#include "gloo/transport/myelon/context.h"

namespace gloo {
namespace transport {
namespace myelon {

UnboundBuffer::UnboundBuffer(std::shared_ptr<Context> context, void* ptr, size_t size)
    : ::gloo::transport::UnboundBuffer(ptr, size), context_(std::move(context)) {}

UnboundBuffer::~UnboundBuffer() = default;

void UnboundBuffer::handleRecvCompletion(int rank) {
  std::lock_guard<std::mutex> lock(mutex_);
  recvCompletions_++;
  recvRank_ = rank;
  recvCv_.notify_one();
}

void UnboundBuffer::handleRecvError(std::exception_ptr error) {
  std::lock_guard<std::mutex> lock(mutex_);
  recvException_ = std::move(error);
  recvCv_.notify_one();
}

void UnboundBuffer::handleSendCompletion(int rank) {
  std::lock_guard<std::mutex> lock(mutex_);
  sendCompletions_++;
  sendRank_ = rank;
  sendCv_.notify_one();
}

void UnboundBuffer::handleSendError(std::exception_ptr error) {
  std::lock_guard<std::mutex> lock(mutex_);
  sendException_ = std::move(error);
  sendCv_.notify_one();
}

void UnboundBuffer::abortWaitRecv() {
  std::lock_guard<std::mutex> guard(mutex_);
  abortWaitRecv_ = true;
  recvCv_.notify_one();
}

void UnboundBuffer::abortWaitSend() {
  std::lock_guard<std::mutex> guard(mutex_);
  abortWaitSend_ = true;
  sendCv_.notify_one();
}

bool UnboundBuffer::waitRecv(int* rank, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (timeout == kUnsetTimeout) {
    timeout = context_->getTimeout();
  }

  if (recvCompletions_ == 0) {
    const auto done = recvCv_.wait_for(
        lock,
        timeout,
        [&] { return abortWaitRecv_ || recvCompletions_ > 0 || recvException_ != nullptr; });
    if (!done) {
      throw ::gloo::IoException(GLOO_ERROR_MSG(
          "Timed out waiting ",
          timeout.count(),
          "ms for Myelon recv operation to complete"));
    }
  }

  if (abortWaitRecv_) {
    abortWaitRecv_ = false;
    return false;
  }

  if (recvException_ != nullptr) {
    auto error = recvException_;
    recvException_ = nullptr;
    std::rethrow_exception(error);
  }

  recvCompletions_--;
  if (rank != nullptr) {
    *rank = recvRank_;
  }
  return true;
}

bool UnboundBuffer::waitSend(int* rank, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (timeout == kUnsetTimeout) {
    timeout = context_->getTimeout();
  }

  if (sendCompletions_ == 0) {
    const auto done = sendCv_.wait_for(
        lock,
        timeout,
        [&] { return abortWaitSend_ || sendCompletions_ > 0 || sendException_ != nullptr; });
    if (!done) {
      throw ::gloo::IoException(GLOO_ERROR_MSG(
          "Timed out waiting ",
          timeout.count(),
          "ms for Myelon send operation to complete"));
    }
  }

  if (abortWaitSend_) {
    abortWaitSend_ = false;
    return false;
  }

  if (sendException_ != nullptr) {
    auto error = sendException_;
    sendException_ = nullptr;
    std::rethrow_exception(error);
  }

  sendCompletions_--;
  if (rank != nullptr) {
    *rank = sendRank_;
  }
  return true;
}

void UnboundBuffer::send(int dstRank, uint64_t tag, size_t offset, size_t nbytes) {
  if (nbytes == kUnspecifiedByteCount) {
    GLOO_ENFORCE_LE(offset, size);
    nbytes = size - offset;
  }
  context_->getPair(dstRank)->send(this, tag, offset, nbytes);
}

void UnboundBuffer::recv(int srcRank, uint64_t tag, size_t offset, size_t nbytes) {
  if (nbytes == kUnspecifiedByteCount) {
    GLOO_ENFORCE_LE(offset, size);
    nbytes = size - offset;
  }
  context_->getPair(srcRank)->recv(this, tag, offset, nbytes);
}

void UnboundBuffer::recv(
    std::vector<int> srcRanks,
    uint64_t tag,
    size_t offset,
    size_t nbytes) {
  GLOO_ENFORCE_EQ(
      srcRanks.size(),
      1,
      "Myelon transport currently supports recv from exactly one source rank");
  recv(srcRanks.front(), tag, offset, nbytes);
}

} // namespace myelon
} // namespace transport
} // namespace gloo
