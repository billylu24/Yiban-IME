#include "dictionary/bilingual_dictionary.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace bilingual {
namespace {

std::vector<std::string> splitAlternatives(std::string_view value) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const auto end = value.find('|', begin);
    const auto part =
        value.substr(begin, end == std::string_view::npos ? value.size() - begin
                                                          : end - begin);
    if (!part.empty()) {
      result.emplace_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return result;
}

std::size_t utf8Prefix(std::string_view value, std::size_t maxBytes) {
  std::size_t offset = 0;
  std::size_t lastValid = 0;
  while (offset < value.size() && offset < maxBytes) {
    const auto lead = static_cast<unsigned char>(value[offset]);
    std::size_t width = 1;
    if ((lead & 0x80U) == 0) {
      width = 1;
    } else if ((lead & 0xE0U) == 0xC0U) {
      width = 2;
    } else if ((lead & 0xF0U) == 0xE0U) {
      width = 3;
    } else {
      width = 4;
    }
    if (offset + width > maxBytes) {
      break;
    }
    offset += width;
    lastValid = offset;
  }
  return lastValid;
}

} // namespace

std::size_t StringHash::operator()(std::string_view value) const noexcept {
  // Stable FNV-1a keeps string and string_view hashing identical, which lets
  // C++17 lookup inspect one bucket without allocating a temporary string.
  std::size_t hash = sizeof(std::size_t) == 8
                         ? static_cast<std::size_t>(14695981039346656037ULL)
                         : static_cast<std::size_t>(2166136261U);
  const std::size_t prime = sizeof(std::size_t) == 8
                                ? static_cast<std::size_t>(1099511628211ULL)
                                : static_cast<std::size_t>(16777619U);
  for (const auto byte : value) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= prime;
  }
  return hash;
}

bool isValidUtf8(std::string_view text) noexcept {
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t width = 0;
    std::uint32_t codepoint = 0;
    if (lead <= 0x7F) {
      width = 1;
      codepoint = lead;
    } else if (lead >= 0xC2 && lead <= 0xDF) {
      width = 2;
      codepoint = lead & 0x1F;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      width = 3;
      codepoint = lead & 0x0F;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      width = 4;
      codepoint = lead & 0x07;
    } else {
      return false;
    }
    if (i + width > text.size()) {
      return false;
    }
    for (std::size_t j = 1; j < width; ++j) {
      const auto byte = static_cast<unsigned char>(text[i + j]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      codepoint = (codepoint << 6U) | (byte & 0x3FU);
    }
    if ((width == 3 && codepoint < 0x800) ||
        (width == 4 && codepoint < 0x10000) ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF) {
      return false;
    }
    i += width;
  }
  return true;
}

BilingualDictionary BilingualDictionary::loadTsv(const std::string &path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("cannot open bilingual dictionary: " + path);
  }

  BilingualDictionary dictionary;
  std::string line;
  std::size_t lineNumber = 0;
  while (std::getline(stream, line)) {
    ++lineNumber;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line.front() == '#') {
      continue;
    }

    const auto firstTab = line.find('\t');
    const auto secondTab = firstTab == std::string::npos
                               ? std::string::npos
                               : line.find('\t', firstTab + 1);
    if (firstTab == std::string::npos) {
      throw std::runtime_error("invalid dictionary entry at " + path + ":" +
                               std::to_string(lineNumber));
    }
    const std::string_view chinese(line.data(), firstTab);
    const std::string_view primary(
        line.data() + firstTab + 1,
        (secondTab == std::string::npos ? line.size() : secondTab) - firstTab -
            1);
    const std::string_view alternatives(
        secondTab == std::string::npos ? nullptr : line.data() + secondTab + 1,
        secondTab == std::string::npos ? 0 : line.size() - secondTab - 1);

    if (chinese.empty() || primary.empty() || !isValidUtf8(chinese) ||
        !isValidUtf8(primary) || !isValidUtf8(alternatives)) {
      throw std::runtime_error("invalid dictionary entry at " + path + ":" +
                               std::to_string(lineNumber));
    }

    WordTranslation value{std::string(primary),
                          splitAlternatives(alternatives)};
    const auto [unused, inserted] =
        dictionary.entries_.emplace(std::string(chinese), std::move(value));
    if (!inserted) {
      throw std::runtime_error("duplicate dictionary key at " + path + ":" +
                               std::to_string(lineNumber));
    }
  }
  if (!stream.eof()) {
    throw std::runtime_error("failed while reading bilingual dictionary: " +
                             path);
  }
  return dictionary;
}

const WordTranslation *
BilingualDictionary::lookup(std::string_view chinese) const noexcept {
  const auto iter = entries_.find(chinese);
  return iter == entries_.end() ? nullptr : &iter->second;
}

std::string
BilingualDictionary::translateCandidate(std::string_view chinese,
                                        std::size_t maxBytes) const {
  if (const auto *exact = lookup(chinese)) {
    return formatWordHint(*exact, maxBytes);
  }
  if (chinese.empty() || maxBytes == 0 || !isValidUtf8(chinese)) {
    return {};
  }

  std::vector<std::size_t> boundaries{0};
  for (std::size_t offset = 1; offset < chinese.size(); ++offset) {
    if ((static_cast<unsigned char>(chinese[offset]) & 0xC0U) != 0x80U) {
      boundaries.push_back(offset);
    }
  }
  boundaries.push_back(chinese.size());

  struct Segment {
    const WordTranslation *translation = nullptr;
    std::size_t next = 0;
  };
  std::vector<Segment> segments(boundaries.size());
  segments.back().next = boundaries.size();
  for (std::size_t i = boundaries.size() - 1; i-- > 0;) {
    for (std::size_t j = boundaries.size() - 1; j > i; --j) {
      if (j != boundaries.size() - 1 && !segments[j].translation) {
        continue;
      }
      if (const auto *entry = lookup(
              chinese.substr(boundaries[i], boundaries[j] - boundaries[i]))) {
        segments[i] = {entry, j};
        break;
      }
    }
  }
  if (!segments.front().translation) {
    return {};
  }

  std::string result;
  for (std::size_t segment = 0; segment < boundaries.size() - 1;
       segment = segments[segment].next) {
    const auto *best = segments[segment].translation;
    const auto remaining = maxBytes - result.size();
    const auto hint = formatWordHint(*best, remaining, 0);
    if (hint.empty()) {
      return result;
    }
    if (!result.empty()) {
      if (result.size() + 3 > maxBytes) {
        return result;
      }
      result.append(" / ");
    }
    if (result.size() + hint.size() > maxBytes) {
      return result;
    }
    result.append(hint);
  }
  return result;
}

std::string formatWordHint(const WordTranslation &translation,
                           std::size_t maxBytes, std::size_t maxAlternatives) {
  if (maxBytes == 0 || translation.primary.empty()) {
    return {};
  }
  if (!isValidUtf8(translation.primary)) {
    return {};
  }
  if (translation.primary.size() > maxBytes) {
    return translation.primary.substr(
        0, utf8Prefix(translation.primary, maxBytes));
  }

  std::string result = translation.primary;
  std::size_t count = 0;
  for (const auto &alternative : translation.alternatives) {
    if (count >= maxAlternatives || alternative.empty() ||
        alternative == translation.primary || !isValidUtf8(alternative)) {
      continue;
    }
    constexpr std::string_view separator = " / ";
    if (result.size() + separator.size() + alternative.size() > maxBytes) {
      break;
    }
    result.append(separator);
    result.append(alternative);
    ++count;
  }
  return result;
}

std::string mergeCandidateComment(std::string_view existing,
                                  std::string_view hint,
                                  std::string_view separator) {
  if (hint.empty() || existing == hint) {
    return std::string(existing);
  }
  if (existing.empty()) {
    return std::string(hint);
  }
  std::string result;
  result.reserve(existing.size() + separator.size() + hint.size());
  result.append(existing);
  result.append(separator);
  result.append(hint);
  return result;
}

} // namespace bilingual
