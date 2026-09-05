#include "translation/protocol.h"

#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>

namespace bilingual {
namespace {
using Json = nlohmann::json;

std::vector<std::uint8_t> frame(const Json &json) {
    const auto payload = json.dump();
    if (payload.empty() || payload.size() > kMaximumFrameSize) {
        return {};
    }
    std::vector<std::uint8_t> result(4 + payload.size());
    const auto size = static_cast<std::uint32_t>(payload.size());
    result[0] = static_cast<std::uint8_t>(size >> 24U);
    result[1] = static_cast<std::uint8_t>(size >> 16U);
    result[2] = static_cast<std::uint8_t>(size >> 8U);
    result[3] = static_cast<std::uint8_t>(size);
    std::memcpy(result.data() + 4, payload.data(), payload.size());
    return result;
}

std::optional<Json> parse(std::string_view payload) {
    if (payload.empty() || payload.size() > kMaximumFrameSize) {
        return std::nullopt;
    }
    try {
        auto json = Json::parse(payload);
        if (!json.is_object() || json.value("version", 0) != 1) {
            return std::nullopt;
        }
        return json;
    } catch (...) {
        return std::nullopt;
    }
}
} // namespace

std::vector<std::uint8_t>
encodeRequestFrame(const TranslationRequest &request) {
    return frame({{"version", 1},
                  {"id", request.id},
                  {"context", request.context},
                  {"generation", request.generation},
                  {"text", request.text},
                  {"final", request.final}});
}

std::vector<std::uint8_t>
encodeResponseFrame(const TranslationResponse &response) {
    return frame({{"version", 1},
                  {"id", response.id},
                  {"context", response.context},
                  {"generation", response.generation},
                  {"translation", response.translation}});
}

std::optional<TranslationRequest> decodeRequest(std::string_view payload) {
    const auto json = parse(payload);
    if (!json) {
        return std::nullopt;
    }
    try {
        TranslationRequest result;
        result.id = json->at("id").get<std::uint64_t>();
        result.context = json->at("context").get<std::string>();
        result.generation = json->at("generation").get<std::uint64_t>();
        result.text = json->at("text").get<std::string>();
        result.final = json->at("final").get<bool>();
        if (result.context.empty() || result.text.empty()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TranslationResponse> decodeResponse(std::string_view payload) {
    const auto json = parse(payload);
    if (!json) {
        return std::nullopt;
    }
    try {
        TranslationResponse result;
        result.id = json->at("id").get<std::uint64_t>();
        result.context = json->at("context").get<std::string>();
        result.generation = json->at("generation").get<std::uint64_t>();
        result.translation = json->at("translation").get<std::string>();
        if (result.context.empty() || result.translation.empty()) {
            return std::nullopt;
        }
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

bool FrameParser::append(const std::uint8_t *data, std::size_t size) {
    if (failed_ || size > kMaximumFrameSize + 4 ||
        buffer_.size() + size > (kMaximumFrameSize + 4) * 2) {
        failed_ = true;
        return false;
    }
    buffer_.insert(buffer_.end(), data, data + size);
    if (buffer_.size() >= 4) {
        const auto length = (static_cast<std::uint32_t>(buffer_[0]) << 24U) |
                            (static_cast<std::uint32_t>(buffer_[1]) << 16U) |
                            (static_cast<std::uint32_t>(buffer_[2]) << 8U) |
                            static_cast<std::uint32_t>(buffer_[3]);
        if (length == 0 || length > kMaximumFrameSize) {
            failed_ = true;
            return false;
        }
    }
    return true;
}

std::optional<std::string> FrameParser::next() {
    if (failed_ || buffer_.size() < 4) {
        return std::nullopt;
    }
    const auto length = (static_cast<std::uint32_t>(buffer_[0]) << 24U) |
                        (static_cast<std::uint32_t>(buffer_[1]) << 16U) |
                        (static_cast<std::uint32_t>(buffer_[2]) << 8U) |
                        static_cast<std::uint32_t>(buffer_[3]);
    if (length == 0 || length > kMaximumFrameSize) {
        failed_ = true;
        return std::nullopt;
    }
    if (buffer_.size() < length + 4) {
        return std::nullopt;
    }
    std::string payload(reinterpret_cast<const char *>(buffer_.data() + 4),
                        length);
    buffer_.erase(buffer_.begin(), buffer_.begin() + 4 + length);
    return payload;
}

void FrameParser::reset() {
    buffer_.clear();
    failed_ = false;
}

} // namespace bilingual
