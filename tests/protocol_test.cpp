#include "translation/protocol.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    bilingual::TranslationRequest request{7, "ctx-1", 42, "你好。", true};
    const auto bytes = bilingual::encodeRequestFrame(request);
    require(bytes.size() > 4, "request encoded");

    bilingual::FrameParser parser;
    require(parser.append(bytes.data(), 2), "first partial frame accepted");
    require(!parser.next(), "partial header waits");
    require(parser.append(bytes.data() + 2, bytes.size() - 2),
            "remaining frame accepted");
    auto payload = parser.next();
    require(payload.has_value(), "complete frame returned");
    auto decoded = bilingual::decodeRequest(*payload);
    require(decoded && decoded->id == 7 && decoded->context == "ctx-1" &&
                decoded->generation == 42 && decoded->text == "你好。" &&
                decoded->final,
            "request round trip");

    auto duplicate = bytes;
    duplicate.insert(duplicate.end(), bytes.begin(), bytes.end());
    parser.reset();
    require(parser.append(duplicate.data(), duplicate.size()),
            "multiple frames accepted");
    require(parser.next().has_value() && parser.next().has_value(),
            "multiple frames parsed");

    const std::uint8_t oversized[] = {0, 2, 0, 1};
    parser.reset();
    require(!parser.append(oversized, sizeof(oversized)) && parser.failed(),
            "oversized frame rejected");
    require(!bilingual::decodeResponse("{bad json"), "malformed JSON rejected");
    return 0;
}
