#ifndef BILINGUAL_IME_TRANSLATION_PROTOCOL_H
#define BILINGUAL_IME_TRANSLATION_PROTOCOL_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bilingual {

constexpr std::size_t kMaximumFrameSize = 64 * 1024;

struct TranslationRequest {
    std::uint64_t id = 0;
    std::string context;
    std::uint64_t generation = 0;
    std::string text;
    bool final = false;
};

struct TranslationResponse {
    std::uint64_t id = 0;
    std::string context;
    std::uint64_t generation = 0;
    std::string translation;
};

std::vector<std::uint8_t> encodeRequestFrame(const TranslationRequest &request);
std::vector<std::uint8_t>
encodeResponseFrame(const TranslationResponse &response);
std::optional<TranslationRequest> decodeRequest(std::string_view json);
std::optional<TranslationResponse> decodeResponse(std::string_view json);

class FrameParser {
public:
    bool append(const std::uint8_t *data, std::size_t size);
    std::optional<std::string> next();
    bool failed() const { return failed_; }
    void reset();

private:
    std::vector<std::uint8_t> buffer_;
    bool failed_ = false;
};

} // namespace bilingual

#endif
