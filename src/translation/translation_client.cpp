#include "translation/translation_client.h"

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace bilingual {

TranslationClient::TranslationClient(std::string socketPath,
                                     std::function<void()> notify, int timeoutMs)
    : socketPath_(std::move(socketPath)), notify_(std::move(notify)), timeoutMs_(timeoutMs),
      worker_([this] { run(); }) {}

TranslationClient::~TranslationClient() {
    stopping_ = true;
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    disconnect();
}

void TranslationClient::submit(TranslationRequest request) {
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(request);
    }
    condition_.notify_one();
}

std::vector<TranslationResponse> TranslationClient::takeResponses() {
    std::lock_guard lock(mutex_);
    auto result = std::move(responses_);
    responses_.clear();
    return result;
}

void TranslationClient::run() {
    while (!stopping_) {
        std::optional<TranslationRequest> request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock,
                            [this] { return stopping_ || pending_.has_value(); });
            if (stopping_) {
                return;
            }
            request = std::move(pending_);
            pending_.reset();
        }

        TranslationResponse response;
        bool succeeded = ensureConnected() && exchange(*request, response);
        if (!succeeded && !stopping_) {
            disconnect();
            succeeded = ensureConnected() && exchange(*request, response);
        }
        if (succeeded) {
            {
                std::lock_guard lock(mutex_);
                responses_.push_back(std::move(response));
            }
            notify_();
        } else {
            disconnect();
        }
    }
}

bool TranslationClient::ensureConnected() {
    if (socket_ >= 0) {
        return true;
    }
    if (socketPath_.size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
    }
    socket_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_ < 0) {
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socketPath_.c_str(), socketPath_.size() + 1);
    if (::connect(socket_, reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) != 0) {
        disconnect();
        return false;
    }
    return true;
}

bool TranslationClient::waitFor(short events, int timeoutMs) {
    pollfd descriptor{socket_, events, 0};
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!stopping_) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            return false;
        }
        const auto result = ::poll(&descriptor, 1, static_cast<int>(std::min<long long>(100, remaining)));
        if (result > 0) {
            return (descriptor.revents & events) != 0;
        }
        if (result == 0) {
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
    return false;
}

bool TranslationClient::writeAll(const std::vector<std::uint8_t> &data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        if (!waitFor(POLLOUT, 1000)) {
            return false;
        }
        const auto written = ::send(socket_, data.data() + offset,
                                    data.size() - offset, MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

bool TranslationClient::readExact(void *data, std::size_t size) {
    auto *output = static_cast<std::uint8_t *>(data);
    std::size_t offset = 0;
    while (offset < size) {
        if (!waitFor(POLLIN, timeoutMs_)) {
            return false;
        }
        const auto received = ::recv(socket_, output + offset, size - offset, 0);
        if (received <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(received);
    }
    return true;
}

bool TranslationClient::exchange(const TranslationRequest &request,
                                 TranslationResponse &response) {
    const auto outgoing = encodeRequestFrame(request);
    if (outgoing.empty() || !writeAll(outgoing)) {
        return false;
    }
    std::uint8_t header[4];
    if (!readExact(header, sizeof(header))) {
        return false;
    }
    const auto size = (static_cast<std::uint32_t>(header[0]) << 24U) |
                      (static_cast<std::uint32_t>(header[1]) << 16U) |
                      (static_cast<std::uint32_t>(header[2]) << 8U) |
                      static_cast<std::uint32_t>(header[3]);
    if (size == 0 || size > kMaximumFrameSize) {
        return false;
    }
    std::string payload(size, '\0');
    if (!readExact(payload.data(), payload.size())) {
        return false;
    }
    auto decoded = decodeResponse(payload);
    if (!decoded || decoded->id != request.id ||
        decoded->context != request.context) {
        return false;
    }
    response = std::move(*decoded);
    return true;
}

void TranslationClient::disconnect() {
    if (socket_ >= 0) {
        ::close(socket_);
        socket_ = -1;
    }
}

} // namespace bilingual
