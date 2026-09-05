#ifndef BILINGUAL_IME_ADDON_BILINGUAL_CONTEXT_H
#define BILINGUAL_IME_ADDON_BILINGUAL_CONTEXT_H

#include "context/sentence_state.h"
#include "translation/translation_client.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <fcitx-config/configuration.h>
#include <fcitx-config/option.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/handlertable.h>
#include <fcitx-utils/trackableobject.h>
#include <fcitx/addoninstance.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/instance.h>

namespace bilingual {

FCITX_CONFIGURATION(
    BilingualContextConfig,
    fcitx::Option<bool> enabled{this, "Enabled", "Enable sentence translation", true};
    fcitx::Option<std::string> socketPath{this, "SocketPath", "Translator socket (empty: automatic)", ""};
    fcitx::Option<int, fcitx::IntConstrain> debounceMs{
        this, "DebounceMs", "Translation debounce (milliseconds)", 200,
        fcitx::IntConstrain(0, 5000)};
    fcitx::Option<std::string> backend{this, "Backend", "Translation backend", "ollama"};
    fcitx::Option<std::string> ollamaUrl{this, "OllamaUrl", "Ollama server URL", "http://127.0.0.1:11434"};
    fcitx::Option<std::string> ollamaModel{this, "OllamaModel", "Ollama model", "qwen3.5:0.8b"};
    fcitx::Option<std::string> keepAlive{this, "KeepAlive", "Model keep-alive", "5m"};
    fcitx::Option<int, fcitx::IntConstrain> timeoutSeconds{
        this, "TimeoutSeconds", "Model timeout (seconds)", 30,
        fcitx::IntConstrain(1, 300)};
    fcitx::Option<bool> warmup{this, "Warmup", "Warm model when service starts", true};
    fcitx::Option<std::string> argosModelPath{this, "ArgosModelPath", "Argos model directory", ""};
    fcitx::Option<std::vector<std::string>> sentenceBoundaries{
        this,
        "SentenceBoundaries",
        "Characters that finish a translation unit; use 'space' for Space",
        {"space", ".", "!", "?", "。", "！", "？"}};);

class BilingualContextProperty final : public fcitx::InputContextProperty {
public:
  BilingualContextProperty(std::string token, fcitx::InputContext &context)
      : token(std::move(token)), context(context.watch()) {}

  std::string token;
  fcitx::TrackableObjectReference<fcitx::InputContext> context;
  SentenceState sentence;
  CursorText cursorText;
  std::unique_ptr<fcitx::EventSourceTime> debounce;
  std::string injectedAuxDown;
  std::string originalAuxDown;
  std::uint64_t spaceSequence = 0;
  bool finishOnNextCommit = false;
};

class BilingualContextAddon final
    : public fcitx::AddonInstance,
      public fcitx::TrackableObject<BilingualContextAddon> {
public:
  explicit BilingualContextAddon(fcitx::Instance *instance);
  ~BilingualContextAddon() override;
  void reloadConfig() override;
  const fcitx::Configuration *getConfig() const override { return &config_; }
  void setConfig(const fcitx::RawConfig &config) override;

private:
  BilingualContextProperty *property(fcitx::InputContext *context);
  void applyConfig();
  void onCommit(fcitx::CommitStringEvent &event);
  void onKeyPre(fcitx::KeyEvent &event);
  void onKey(fcitx::KeyEvent &event);
  void finishAfterSpace(BilingualContextProperty *property);
  void onSurroundingText(fcitx::InputContext *context);
  void refreshSentence(BilingualContextProperty *state, bool immediate);
  void hideSentence(BilingualContextProperty *state);
  void reset(fcitx::InputContext *context, bool valid);
  void schedule(BilingualContextProperty *property,
                const SentenceRequest &request);
  void submit(const std::string &context, const SentenceRequest &request);
  void consumeResponses();
  void display(BilingualContextProperty *property);
  void clearDisplay(BilingualContextProperty *property);
  bool isRime(fcitx::InputContext *context) const;

  fcitx::Instance *instance_;
  std::uint64_t nextContext_ = 1;
  std::uint64_t nextRequest_ = 1;
  fcitx::FactoryFor<BilingualContextProperty> propertyFactory_;
  std::vector<std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>>>
      eventHandlers_;
  std::unique_ptr<TranslationClient> client_;
  BilingualContextConfig config_;
};

} // namespace bilingual

#endif
