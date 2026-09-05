#include "context/sentence_state.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
} // namespace

int main() {
  const std::vector<std::string> boundaries{"space", ".",  "!", "?",
                                            "。",    "！", "？"};
  bilingual::SentenceState state;
  auto first = state.appendCommitted("我觉得", boundaries);
  require(first.size() == 1 && !first[0].final, "partial request");
  const auto staleGeneration = first[0].generation;

  auto second = state.appendCommitted("这个方法不太好", boundaries);
  require(second.size() == 1 && second[0].text == "我觉得这个方法不太好",
          "commits form a sentence");
  require(!state.acceptTranslation(staleGeneration, "stale"),
          "stale response rejected");
  require(state.acceptTranslation(second[0].generation,
                                  "I do not think this is very good."),
          "current response accepted");

  auto final = state.appendCommitted("。", boundaries);
  require(final.size() == 1 && final[0].final, "Chinese period is final");
  require(final[0].text == "我觉得这个方法不太好。", "final sentence text");
  require(state.activeSentence().empty(), "active sentence cleared");

  auto multiple = state.appendCommitted("你好！今天天气很好", boundaries);
  require(multiple.size() == 2, "boundary and trailing sentence requests");
  require(multiple[0].final && multiple[0].text == "你好！",
          "completed sentence emitted");
  require(!multiple[1].final && multiple[1].text == "今天天气很好",
          "trailing sentence debounced");

  state.invalidate();
  require(!state.valid(), "editing invalidates state");
  require(!state.acceptTranslation(multiple[1].generation, "stale"),
          "invalid state rejects response");
  auto recovered = state.appendCommitted("新句子", boundaries);
  require(state.valid() && recovered.size() == 1,
          "new commit recovers invalid state");

  auto spaced = state.appendCommitted("第一句 第二句", boundaries);
  require(spaced.size() == 2, "space splits translation units");
  require(spaced[0].final && spaced[0].text == "新句子第一句",
          "space finalizes the preceding continuous text");
  require(!spaced[1].final && spaced[1].text == "第二句",
          "text after space starts a new unit");
  require(spaced[0].text.find(' ') == std::string::npos,
          "space is not sent to translator");

  auto explicitFinish = state.finishActiveSentence(" ");
  require(explicitFinish && explicitFinish->final &&
              explicitFinish->text == "第二句",
          "space key can explicitly finish active text");
  require(!state.finishActiveSentence(" "), "empty text is not translated");

  auto reopenSpace = state.eraseBackward();
  require(reopenSpace && !reopenSpace->final && reopenSpace->text == "第二句",
          "deleting space reopens the preceding sentence");
  auto joined = state.appendCommitted("和第三句", boundaries);
  require(joined.size() == 1 && joined[0].text == "第二句和第三句",
          "typing after deleting a boundary joins the sentences");

  bilingual::SentenceState punctuationState;
  auto punctuated = punctuationState.appendCommitted("你好。", boundaries);
  require(punctuated.size() == 1 && punctuated[0].final,
          "punctuation completes a sentence before deletion");
  auto reopenPunctuation = punctuationState.eraseBackward();
  require(reopenPunctuation && reopenPunctuation->text == "你好",
          "deleting punctuation reopens text without that punctuation");

  require(bilingual::SentenceState::continuousTextAfterDeletion(
              "第一句 第二句", 4, 4, false, boundaries) ==
              "第一句第二句",
          "deleting a middle space joins both surrounding sentences");
  require(bilingual::SentenceState::continuousTextAfterDeletion(
              "甲。乙", 1, 1, true, boundaries) == "甲乙",
          "Delete joins text across a punctuation boundary");
  require(bilingual::SentenceState::continuousTextAfterDeletion(
              "你好", 2, 2, false, boundaries) == "你",
          "ordinary Backspace removes only one Unicode character");

  state.reset();
  const std::vector<std::string> customBoundaries{"|"};
  require(!bilingual::SentenceState::isBoundary(" ", customBoundaries),
          "removing space from config disables its boundary");
  auto custom = state.appendCommitted("甲。乙|丙", customBoundaries);
  require(custom.size() == 2, "configured boundaries replace defaults");
  require(custom[0].final && custom[0].text == "甲。乙|",
          "custom boundary is honored");
  require(!custom[1].final && custom[1].text == "丙",
          "custom trailing text remains active");
  require(bilingual::SentenceState::continuousTextAfterDeletion(
              "甲|乙", 2, 2, false, customBoundaries) == "甲乙",
          "custom boundary deletion joins adjacent text");
  return 0;
}
