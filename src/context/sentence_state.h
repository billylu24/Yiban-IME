#ifndef BILINGUAL_IME_CONTEXT_SENTENCE_STATE_H
#define BILINGUAL_IME_CONTEXT_SENTENCE_STATE_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bilingual {

// A local prediction of committed text, reconciled with frontend snapshots.
// Offsets are Unicode codepoints, as in Fcitx SurroundingText.
class CursorText {
public:
  bool update(std::string_view text, std::size_t cursor, std::size_t anchor);
  void commit(std::string_view text);
  void erase(bool forward);
  void finish();
  void reset();
  void awaitCursorUpdate();
  std::string sentence(const std::vector<std::string> &boundaries) const;
  std::string previousSentence(const std::vector<std::string> &boundaries) const;

private:
  struct Snapshot {
    std::string text;
    std::size_t cursor = 0;
    std::size_t anchor = 0;
    bool operator==(const Snapshot &) const = default;
  };
  void replace(std::size_t begin, std::size_t end, std::string_view text);
  std::string sentenceAt(std::size_t cursor,
                         const std::vector<std::string> &boundaries) const;
  Snapshot current_;
  std::deque<Snapshot> pendingSnapshots_;
  std::vector<std::size_t> virtualBoundaries_;
  bool valid_ = false;
};

struct SentenceRequest {
  std::uint64_t generation = 0;
  std::string text;
  bool final = false;
};

class SentenceState {
public:
  std::vector<SentenceRequest>
  appendCommitted(std::string_view text,
                  const std::vector<std::string> &boundaries);
  std::optional<SentenceRequest>
  finishActiveSentence(std::string_view boundary);
  std::optional<SentenceRequest> eraseBackward();
  std::optional<SentenceRequest>
  replaceActiveSentence(std::string_view sentence);
  // Select the sentence under the caret. Empty text hides and invalidates old
  // display requests; exact cached translations are restored synchronously.
  std::optional<SentenceRequest> selectSentence(std::string_view sentence);
  void reset(bool valid = true);
  void invalidate();

  static bool isBoundary(std::string_view codepoint,
                         const std::vector<std::string> &boundaries);
  static std::string continuousTextAfterDeletion(
      std::string_view text, std::size_t cursor, std::size_t anchor,
      bool deleteForward, const std::vector<std::string> &boundaries);

  bool acceptTranslation(std::uint64_t generation, std::string translation);

  const std::string &activeSentence() const { return activeSentence_; }
  const std::string &previousSentence() const { return previousSentence_; }
  const std::string &translation() const { return translation_; }
  std::uint64_t generation() const { return generation_; }
  bool valid() const { return valid_; }

private:
  friend class CursorText;
  SentenceRequest rememberRequest(std::string text, bool final,
                                 std::string cacheKey);
  static bool isWhitespace(std::string_view codepoint);
  static std::vector<std::string_view> codepoints(std::string_view text);
  static void eraseLastCodepoint(std::string &text);
  void trimActiveSentence();
  void clearTranslation();

  struct CompletedSentence {
    std::string body;
    std::string boundary;
  };

  std::string activeSentence_;
  std::string previousSentence_;
  std::string translation_;
  std::vector<CompletedSentence> completedSentences_;
  std::uint64_t generation_ = 0;
  std::uint64_t translatedGeneration_ = 0;
  bool valid_ = true;
  struct RequestedSentence {
    std::uint64_t generation;
    std::string text;
  };
  std::deque<RequestedSentence> requestedSentences_;
  std::deque<std::pair<std::string, std::string>> translationCache_;
};

} // namespace bilingual

#endif
