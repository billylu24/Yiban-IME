#ifndef BILINGUAL_IME_DICTIONARY_BILINGUAL_DICTIONARY_H_
#define BILINGUAL_IME_DICTIONARY_BILINGUAL_DICTIONARY_H_

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace bilingual {

struct StringHash {
  using is_transparent = void;

  std::size_t operator()(std::string_view value) const noexcept;
  std::size_t operator()(const std::string &value) const noexcept {
    return (*this)(std::string_view(value));
  }
};

struct StringEqual {
  using is_transparent = void;

  bool operator()(std::string_view left,
                  std::string_view right) const noexcept {
    return left == right;
  }
};

struct WordTranslation {
  std::string primary;
  std::vector<std::string> alternatives;
};

class BilingualDictionary {
public:
  // The TSV schema is: Chinese<TAB>primary<TAB>alternative 1|alternative 2.
  // Blank lines and lines beginning with '#' are ignored. Loading is the
  // only operation that performs I/O; lookup() is read-only and allocation
  // free.
  static BilingualDictionary loadTsv(const std::string &path);

  const WordTranslation *lookup(std::string_view chinese) const noexcept;
  std::size_t size() const noexcept { return entries_.size(); }

private:
  std::unordered_map<std::string, WordTranslation, StringHash, StringEqual>
      entries_;
};

// Format a compact candidate comment. The returned string is valid UTF-8 and
// never exceeds maxBytes. Alternatives are appended only when they fit in
// full; primary is UTF-8-truncated as a last resort.
std::string formatWordHint(const WordTranslation &translation,
                           std::size_t maxBytes = 48,
                           std::size_t maxAlternatives = 1);

// Preserve an existing Rime comment and append the local hint. Empty and
// duplicate values are handled without adding separators.
std::string mergeCandidateComment(std::string_view existing,
                                  std::string_view hint,
                                  std::string_view separator = " · ");

bool isValidUtf8(std::string_view text) noexcept;

} // namespace bilingual

#endif
