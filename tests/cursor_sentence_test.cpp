#include "context/sentence_state.h"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool value, const char *message) {
  if (!value) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
}

int main() {
  const std::vector<std::string> boundaries{"space", ".", "!", "?", "。", "！", "？"};
  bilingual::CursorText cursor;
  bilingual::SentenceState state;
  cursor.update("我喜欢苹果", 5, 5);
  auto request = state.selectSentence(cursor.sentence(boundaries));
  require(request && request->text == "我喜欢苹果", "sentence under caret");
  require(state.acceptTranslation(request->generation, "I like apples."), "initial translation");
  cursor.commit(" ");
  state.selectSentence(cursor.sentence(boundaries));
  require(state.translation().empty(), "space hides the previous translation");
  require(cursor.previousSentence(boundaries) == "我喜欢苹果", "completed text available for background translation");
  require(!cursor.update("我喜欢苹果", 5, 5), "old snapshot cannot undo a predicted space");
  cursor.update("我喜欢苹果 ", 6, 6);
  require(cursor.sentence(boundaries).empty(), "acknowledged space remains blank");
  cursor.update("我喜欢苹果 ", 2, 2);
  require(!state.selectSentence(cursor.sentence(boundaries)), "return uses cached translation");
  require(state.translation() == "I like apples.", "cached translation appears immediately");

  cursor.commit("很");
  request = state.selectSentence(cursor.sentence(boundaries));
  require(request && request->text == "我喜很欢苹果", "insertion uses actual caret, retaining suffix");
  require(state.translation().empty(), "edited sentence cannot display old translation");
  const auto old = request->generation;
  cursor.update("我喜很欢苹果 ", 7, 7);
  state.selectSentence(cursor.sentence(boundaries));
  require(!state.acceptTranslation(old, "Edited translation"), "late reply stays hidden after leaving sentence");
  require(state.translation().empty(), "late reply did not restore display");
  cursor.update("我喜很欢苹果 ", 3, 3);
  require(!state.selectSentence(cursor.sentence(boundaries)), "late reply is available on revisit");
  require(state.translation() == "Edited translation", "revisit restores late cached result");
  cursor.erase(false);
  require(cursor.sentence(boundaries) == "我喜欢苹果", "backspace in the middle preserves suffix");

  cursor.update("甲。乙", 1, 1);
  cursor.erase(true);
  require(cursor.sentence(boundaries) == "甲乙", "deleting a boundary joins both sides");
  cursor.update("甲乙", 0, 2);
  require(cursor.sentence(boundaries).empty(), "selection hides translation");
  cursor.commit("新句");
  require(cursor.sentence(boundaries) == "新句", "commit replaces selection");

  cursor.reset();
  cursor.commit("你好");
  cursor.finish();
  require(cursor.sentence(boundaries).empty(), "Space selection establishes a virtual boundary");
  cursor.update("你好", 2, 2);
  require(cursor.sentence(boundaries).empty(), "snapshot ack preserves virtual boundary");
  cursor.commit("世界");
  require(cursor.sentence(boundaries) == "世界", "new sentence after virtual boundary");
  cursor.update("你好世界", 1, 1);
  require(cursor.sentence(boundaries) == "你好", "moving back restores prior virtual sentence");
  cursor.commit("呀");
  require(cursor.sentence(boundaries) == "你呀好", "editing prior sentence shifts later boundary");
  cursor.update("你呀好世界", 4, 4);
  require(cursor.sentence(boundaries) == "世界", "later virtual sentence survives preceding insertion");

  cursor.update("你好。", 3, 3);
  require(cursor.sentence(boundaries).empty(), "punctuation leaves empty active unit");
  cursor.update("你好。", 1, 1);
  require(cursor.sentence(boundaries) == "你好", "caret before punctuation selects sentence body");
  cursor.update("你好\n", 3, 3);
  require(cursor.sentence(boundaries).empty(), "newline leaves no active sentence");
  cursor.update("甲😀乙 ", 2, 2);
  cursor.erase(false);
  require(cursor.sentence(boundaries) == "甲乙", "cursor offsets count Unicode codepoints");
  cursor.awaitCursorUpdate();
  require(cursor.sentence(boundaries).empty(), "unknown cursor position hides immediately");
  cursor.update("甲乙 ", 1, 1);
  require(cursor.sentence(boundaries) == "甲乙", "new cursor snapshot restores valid text");

  bilingual::SentenceState isolated;
  require(isolated.selectSentence("我喜欢苹果").has_value(), "cache is isolated per context");
  require(isolated.translation().empty(), "other context does not reuse previous display");
  return 0;
}
