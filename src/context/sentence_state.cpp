#include "context/sentence_state.h"

#include <algorithm>

namespace bilingual {
namespace {
constexpr std::size_t kMaximumSentenceBytes = 4096;
}

bool CursorText::update(std::string_view text, std::size_t cursor,
                        std::size_t anchor) {
  const auto count = SentenceState::codepoints(text).size();
  if (cursor > count || anchor > count) {
    reset();
    return true;
  }
  Snapshot incoming{std::string(text), cursor, anchor};
  if (incoming == current_) {
    const bool changed = !valid_;
    valid_ = true;
    pendingSnapshots_.clear();
    return changed;
  }
  // Frontends may acknowledge earlier commits after we have predicted newer
  // ones. Never roll the caret or displayed sentence back to those snapshots.
  if (std::find(pendingSnapshots_.begin(), pendingSnapshots_.end(), incoming) !=
      pendingSnapshots_.end()) {
    return false;
  }
  if (incoming.text != current_.text) {
    virtualBoundaries_.clear();
  }
  current_ = std::move(incoming);
  pendingSnapshots_.clear();
  valid_ = true;
  return true;
}

void CursorText::replace(std::size_t begin, std::size_t end,
                         std::string_view text) {
  auto chars = SentenceState::codepoints(current_.text);
  const auto byteOffset = [&](std::size_t index) {
    return index == chars.size() ? current_.text.size()
                                : std::size_t(chars[index].data() - current_.text.data());
  };
  const auto inserted = SentenceState::codepoints(text).size();
  pendingSnapshots_.push_back(current_);
  if (pendingSnapshots_.size() > 32) {
    pendingSnapshots_.pop_front();
  }
  std::vector<std::size_t> boundaries;
  for (auto boundary : virtualBoundaries_) {
    if (boundary <= begin) {
      boundaries.push_back(boundary);
    } else if (boundary > end) {
      boundaries.push_back(boundary - (end - begin) + inserted);
    }
  }
  virtualBoundaries_ = std::move(boundaries);
  const auto first = byteOffset(begin);
  const auto last = byteOffset(end);
  current_.text.replace(first, last - first, text);
  current_.cursor = current_.anchor = begin + inserted;
}

void CursorText::commit(std::string_view text) {
  if (!valid_) {
    reset();
    valid_ = true;
  }
  replace(std::min(current_.cursor, current_.anchor),
          std::max(current_.cursor, current_.anchor), text);
}

void CursorText::erase(bool forward) {
  if (!valid_) {
    return;
  }
  auto begin = std::min(current_.cursor, current_.anchor);
  auto end = std::max(current_.cursor, current_.anchor);
  if (begin == end) {
    if (forward) {
      end = std::min(end + 1, SentenceState::codepoints(current_.text).size());
    } else if (begin) {
      --begin;
    }
  }
  replace(begin, end, "");
}

void CursorText::finish() {
  if (valid_ && current_.cursor == current_.anchor &&
      std::find(virtualBoundaries_.begin(), virtualBoundaries_.end(),
                current_.cursor) == virtualBoundaries_.end()) {
    virtualBoundaries_.push_back(current_.cursor);
  }
}

void CursorText::reset() {
  current_ = {};
  pendingSnapshots_.clear();
  virtualBoundaries_.clear();
  valid_ = false;
}

void CursorText::awaitCursorUpdate() {
  valid_ = false;
  pendingSnapshots_.clear();
}

std::string CursorText::sentenceAt(
    std::size_t cursor, const std::vector<std::string> &boundaries) const {
  const auto chars = SentenceState::codepoints(current_.text);
  auto begin = cursor;
  auto end = cursor;
  const auto virtualBoundary = [&](std::size_t position) {
    return std::find(virtualBoundaries_.begin(), virtualBoundaries_.end(),
                     position) != virtualBoundaries_.end();
  };
  const auto separator = [&](std::string_view character) {
    return SentenceState::isBoundary(character, boundaries) ||
           character == "\n" || character == "\r";
  };
  while (begin && !virtualBoundary(begin) && !separator(chars[begin - 1])) {
    --begin;
  }
  while (end < chars.size() && !separator(chars[end]) &&
         (end == cursor || !virtualBoundary(end))) {
    ++end;
  }
  std::string result;
  for (auto i = begin; i < end; ++i) {
    result.append(chars[i]);
  }
  if (std::all_of(result.begin(), result.end(), [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
      })) {
    return {};
  }
  // Do not translate a silently truncated sentence as though it were whole.
  return result.size() <= kMaximumSentenceBytes ? result : std::string{};
}

std::string CursorText::sentence(
    const std::vector<std::string> &boundaries) const {
  if (!valid_ || current_.cursor != current_.anchor) {
    return {};
  }
  return sentenceAt(current_.cursor, boundaries);
}

std::string CursorText::previousSentence(
    const std::vector<std::string> &boundaries) const {
  if (!valid_ || current_.cursor != current_.anchor || !current_.cursor) {
    return {};
  }
  return sentenceAt(current_.cursor - 1, boundaries);
}

std::vector<std::string_view> SentenceState::codepoints(std::string_view text) {
  std::vector<std::string_view> result;
  for (std::size_t offset = 0; offset < text.size();) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    if ((lead & 0xe0U) == 0xc0U) {
      length = 2;
    } else if ((lead & 0xf0U) == 0xe0U) {
      length = 3;
    } else if ((lead & 0xf8U) == 0xf0U) {
      length = 4;
    }
    length = std::min(length, text.size() - offset);
    result.emplace_back(text.substr(offset, length));
    offset += length;
  }
  return result;
}

bool SentenceState::isBoundary(std::string_view codepoint,
                               const std::vector<std::string> &boundaries) {
  return std::any_of(boundaries.begin(), boundaries.end(),
                     [codepoint](const std::string &boundary) {
                       if (boundary == "space" || boundary == "<space>") {
                         return codepoint == " ";
                       }
                       return codepoint == boundary;
                     });
}

bool SentenceState::isWhitespace(std::string_view codepoint) {
  return codepoint == " " || codepoint == "\t" || codepoint == "\n" ||
         codepoint == "\r" || codepoint == "\xE3\x80\x80";
}

std::string SentenceState::continuousTextAfterDeletion(
    std::string_view text, std::size_t cursor, std::size_t anchor,
    bool deleteForward, const std::vector<std::string> &boundaries) {
  auto characters = codepoints(text);
  cursor = std::min(cursor, characters.size());
  anchor = std::min(anchor, characters.size());
  auto eraseBegin = std::min(cursor, anchor);
  auto eraseEnd = std::max(cursor, anchor);

  if (eraseBegin == eraseEnd) {
    if (deleteForward) {
      eraseEnd = std::min(eraseEnd + 1, characters.size());
    } else if (eraseBegin > 0) {
      --eraseBegin;
    }
  }
  characters.erase(characters.begin() + eraseBegin,
                   characters.begin() + eraseEnd);

  const auto caret = eraseBegin;
  auto sentenceBegin = caret;
  while (sentenceBegin > 0 &&
         !isBoundary(characters[sentenceBegin - 1], boundaries)) {
    --sentenceBegin;
  }
  auto sentenceEnd = caret;
  while (sentenceEnd < characters.size() &&
         !isBoundary(characters[sentenceEnd], boundaries)) {
    ++sentenceEnd;
  }

  std::string sentence;
  for (auto index = sentenceBegin; index < sentenceEnd; ++index) {
    sentence.append(characters[index]);
  }
  return sentence;
}

void SentenceState::eraseLastCodepoint(std::string &text) {
  if (text.empty()) {
    return;
  }
  auto offset = text.size() - 1;
  while (offset > 0 &&
         (static_cast<unsigned char>(text[offset]) & 0xc0U) == 0x80U) {
    --offset;
  }
  text.erase(offset);
}

void SentenceState::clearTranslation() {
  translation_.clear();
  translatedGeneration_ = 0;
}

void SentenceState::trimActiveSentence() {
  if (activeSentence_.size() <= kMaximumSentenceBytes) {
    return;
  }
  auto offset = activeSentence_.size() - kMaximumSentenceBytes;
  while (offset < activeSentence_.size() &&
         (static_cast<unsigned char>(activeSentence_[offset]) & 0xc0U) ==
             0x80U) {
    ++offset;
  }
  activeSentence_.erase(0, offset);
}

std::vector<SentenceRequest>
SentenceState::appendCommitted(std::string_view text,
                               const std::vector<std::string> &boundaries) {
  std::vector<SentenceRequest> requests;
  if (text.empty()) {
    return requests;
  }
  if (!valid_) {
    activeSentence_.clear();
    previousSentence_.clear();
    completedSentences_.clear();
    valid_ = true;
  }

  bool hasTrailingText = false;
  clearTranslation();
  for (const auto codepoint : codepoints(text)) {
    if (!isBoundary(codepoint, boundaries)) {
      activeSentence_.append(codepoint);
      hasTrailingText = true;
      continue;
    }
    if (auto request = finishActiveSentence(codepoint)) {
      requests.push_back(std::move(*request));
    }
    hasTrailingText = false;
  }

  if (hasTrailingText) {
    trimActiveSentence();
    ++generation_;
    requests.push_back(rememberRequest(activeSentence_, false, activeSentence_));
  }
  return requests;
}

std::optional<SentenceRequest>
SentenceState::finishActiveSentence(std::string_view boundary) {
  if (!valid_ || activeSentence_.empty()) {
    return std::nullopt;
  }
  trimActiveSentence();
  auto requestText = activeSentence_;
  if (!isWhitespace(boundary)) {
    requestText.append(boundary);
  }
  completedSentences_.push_back(
      CompletedSentence{activeSentence_, std::string(boundary)});
  previousSentence_ = requestText;
  activeSentence_.clear();
  clearTranslation();
  ++generation_;
  return rememberRequest(std::move(requestText), true, completedSentences_.back().body);
}

std::optional<SentenceRequest> SentenceState::eraseBackward() {
  if (!valid_) {
    return std::nullopt;
  }

  if (!activeSentence_.empty()) {
    eraseLastCodepoint(activeSentence_);
  } else if (!completedSentences_.empty()) {
    activeSentence_ = std::move(completedSentences_.back().body);
    completedSentences_.pop_back();
  }

  previousSentence_.clear();
  if (!completedSentences_.empty()) {
    const auto &previous = completedSentences_.back();
    previousSentence_ = previous.body;
    if (!isWhitespace(previous.boundary)) {
      previousSentence_ += previous.boundary;
    }
  }
  clearTranslation();
  ++generation_;
  if (activeSentence_.empty()) {
    return std::nullopt;
  }
  return rememberRequest(activeSentence_, false, activeSentence_);
}

std::optional<SentenceRequest>
SentenceState::replaceActiveSentence(std::string_view sentence) {
  valid_ = true;
  activeSentence_.assign(sentence);
  trimActiveSentence();
  previousSentence_.clear();
  completedSentences_.clear();
  clearTranslation();
  ++generation_;
  if (activeSentence_.empty()) {
    return std::nullopt;
  }
  return rememberRequest(activeSentence_, false, activeSentence_);
}

SentenceRequest SentenceState::rememberRequest(std::string text, bool final,
                                               std::string cacheKey) {
  requestedSentences_.push_back({generation_, std::move(cacheKey)});
  if (requestedSentences_.size() > 64) {
    requestedSentences_.pop_front();
  }
  return {generation_, std::move(text), final};
}

std::optional<SentenceRequest>
SentenceState::selectSentence(std::string_view sentence) {
  if (valid_ && activeSentence_ == sentence) {
    return std::nullopt;
  }
  auto request = replaceActiveSentence(sentence);
  for (const auto &[text, translated] : translationCache_) {
    if (text == activeSentence_ && !text.empty()) {
      translation_ = translated;
      translatedGeneration_ = generation_;
      return std::nullopt;
    }
  }
  return request;
}

void SentenceState::reset(bool valid) {
  activeSentence_.clear();
  previousSentence_.clear();
  completedSentences_.clear();
  clearTranslation();
  valid_ = valid;
  ++generation_;
}

void SentenceState::invalidate() { reset(false); }

bool SentenceState::acceptTranslation(std::uint64_t generation,
                                      std::string translation) {
  if (translation.empty()) {
    return false;
  }
  auto request = std::find_if(requestedSentences_.begin(), requestedSentences_.end(),
                             [generation](const auto &item) {
                               return item.generation == generation;
                             });
  if (request == requestedSentences_.end()) {
    return false;
  }
  const auto text = request->text;
  requestedSentences_.erase(request);
  // Late results may populate the cache, but never restore an old display.
  std::erase_if(translationCache_, [&](const auto &item) { return item.first == text; });
  translationCache_.emplace_back(text, translation);
  if (translationCache_.size() > 32) {
    translationCache_.pop_front();
  }
  if (!valid_ || generation != generation_ || activeSentence_.empty()) {
    return false;
  }
  translation_ = std::move(translation);
  translatedGeneration_ = generation;
  return true;
}

} // namespace bilingual
