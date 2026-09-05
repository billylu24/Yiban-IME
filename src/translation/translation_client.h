#ifndef BILINGUAL_IME_TRANSLATION_CLIENT_H
#define BILINGUAL_IME_TRANSLATION_CLIENT_H

#include "translation/protocol.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bilingual {

class TranslationClient {
public:
    TranslationClient(std::string socketPath, std::function<void()> notify,
                      int timeoutMs = 35000);
    ~TranslationClient();

    TranslationClient(const TranslationClient &) = delete;
    TranslationClient &operator=(const TranslationClient &) = delete;

    void submit(TranslationRequest request);
    std::vector<TranslationResponse> takeResponses();

private:
    void run();
    bool ensureConnected();
    bool exchange(const TranslationRequest &request,
                  TranslationResponse &response);
    bool writeAll(const std::vector<std::uint8_t> &data);
    bool readExact(void *data, std::size_t size);
    bool waitFor(short events, int timeoutMs);
    void disconnect();

    std::string socketPath_;
    std::function<void()> notify_;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<TranslationRequest> pending_;
    std::vector<TranslationResponse> responses_;
    int timeoutMs_;
    std::thread worker_;
    int socket_ = -1;
};

} // namespace bilingual

#endif
