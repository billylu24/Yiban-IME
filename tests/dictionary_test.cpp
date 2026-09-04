#include "dictionary/bilingual_dictionary.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

std::filesystem::path writeFixture(const std::string &name,
                                   const std::string &content) {
  const auto path = std::filesystem::temp_directory_path() / name;
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream << content;
  return path;
}

template <typename Function>
void expectThrows(Function function, const char *message) {
  try {
    function();
    expect(false, message);
  } catch (const std::runtime_error &) {
  }
}

} // namespace

int main(int argc, char **argv) {
  const auto validPath = writeFixture(
      "bilingual-ime-dictionary-valid.tsv",
      "# fixture\n你好\thello\thi|greetings\n你\tyou\t\n好\tgood\tfine\n"
      "你方\twrong dead end\t\n"
      "方法\tmethod\tway|approach\r\n");
  const auto dictionary =
      bilingual::BilingualDictionary::loadTsv(validPath.string());
  expect(dictionary.size() == 5, "loads comments, LF, and CRLF");
  const auto *hello = dictionary.lookup("你好");
  expect(hello && hello->primary == "hello", "looks up primary meaning");
  expect(hello && hello->alternatives.size() == 2, "loads alternatives");
  expect(dictionary.lookup("不存在") == nullptr, "reports a miss");
  expect(dictionary.translateCandidate("你好") == "hello / hi",
         "translates an exact candidate");
  expect(dictionary.translateCandidate("你方法") == "you / method",
         "backtracks when the longest segmentation cannot complete");
  expect(dictionary.translateCandidate("你不存在").empty(),
         "does not emit a partial translation when segmentation fails");

  expect(bilingual::formatWordHint(*hello) == "hello / hi",
         "formats primary and one alternative");
  expect(bilingual::formatWordHint(*hello, 7) == "hello",
         "does not partially append an alternative");
  const bilingual::WordTranslation unicode{"你好世界", {}};
  const auto truncated = bilingual::formatWordHint(unicode, 7);
  expect(truncated == "你好" && bilingual::isValidUtf8(truncated),
         "truncates only at a UTF-8 boundary");
  const bilingual::WordTranslation invalidPrimary{
      std::string(1, static_cast<char>(0xFF)), {}};
  expect(bilingual::formatWordHint(invalidPrimary).empty(),
         "does not emit invalid UTF-8 from direct API callers");

  expect(bilingual::mergeCandidateComment("字典注释", "method") ==
             "字典注释 · method",
         "preserves an existing Rime comment");
  expect(bilingual::mergeCandidateComment("", "method") == "method",
         "does not add a leading separator");
  expect(bilingual::mergeCandidateComment("method", "method") == "method",
         "does not duplicate an identical comment");

  const auto duplicatePath =
      writeFixture("bilingual-ime-dictionary-duplicate.tsv",
                   "方法\tmethod\tway\n方法\tapproach\tplan\n");
  expectThrows(
      [&] { bilingual::BilingualDictionary::loadTsv(duplicatePath.string()); },
      "rejects duplicate keys");

  const auto invalidPath =
      writeFixture("bilingual-ime-dictionary-invalid.tsv",
                   std::string("坏\tbad\t") + static_cast<char>(0xFF) + "\n");
  expectThrows(
      [&] { bilingual::BilingualDictionary::loadTsv(invalidPath.string()); },
      "rejects invalid UTF-8");
  const auto malformedPath =
      writeFixture("bilingual-ime-dictionary-malformed.tsv", "missing tabs\n");
  expectThrows(
      [&] { bilingual::BilingualDictionary::loadTsv(malformedPath.string()); },
      "rejects a row without a tab");
  expectThrows(
      [] { bilingual::BilingualDictionary::loadTsv("/definitely/missing"); },
      "reports a missing data file");

  if (argc == 2) {
    const auto fullDictionary =
        bilingual::BilingualDictionary::loadTsv(argv[1]);
    expect(fullDictionary.size() > 100000,
           "generated candidate dictionary has broad coverage");
    expect(fullDictionary.translateCandidate("你好") == "hello; hi",
           "full dictionary translates 你好");
    expect(fullDictionary.translateCandidate("你") == "you",
           "full dictionary translates 你 compactly");
    expect(fullDictionary.translateCandidate("拟") == "to plan to / to draft",
           "full dictionary prioritizes the canonical meaning of 拟");
  }

  std::filesystem::remove(validPath);
  std::filesystem::remove(duplicatePath);
  std::filesystem::remove(invalidPath);
  std::filesystem::remove(malformedPath);
  return failures == 0 ? 0 : 1;
}
