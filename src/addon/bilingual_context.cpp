#include "addon/bilingual_context.h"

#include <cstdlib>
#include <utility>
#include <unistd.h>

#include <fcitx-config/iniparser.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/log.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>

namespace bilingual {
namespace {
constexpr std::string_view kHintPrefix = "EN: ";

std::string socketPath(const std::string &configuredPath) {
  if (const auto *configured = std::getenv("BILINGUAL_TRANSLATOR_SOCKET")) {
    return configured;
  }
  if (!configuredPath.empty()) {
    return configuredPath;
  }
  if (const auto *runtime = std::getenv("XDG_RUNTIME_DIR")) {
    return std::string(runtime) + "/bilingual-ime/translator.sock";
  }
  return "/tmp/bilingual-ime-" + std::to_string(::getuid()) + "/translator.sock";
}

} // namespace

BilingualContextAddon::BilingualContextAddon(fcitx::Instance *instance)
    : instance_(instance),
      propertyFactory_([this](fcitx::InputContext &context) {
        return new BilingualContextProperty(std::to_string(nextContext_++),
                                            context);
      }) {
  instance_->inputContextManager().registerProperty("bilingualSentenceState",
                                                    &propertyFactory_);

  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextCommitString>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::CommitStringEvent &event) { onCommit(event); }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextFocusIn>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::FocusInEvent &event) {
            reset(event.inputContext(), true);
            onSurroundingText(event.inputContext());
          }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextFocusOut>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::FocusOutEvent &event) {
            reset(event.inputContext(), false);
          }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextReset>(
          fcitx::EventWatcherPhase::Default, [this](fcitx::ResetEvent &event) {
            reset(event.inputContext(), true);
          }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextSwitchInputMethod>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::InputContextSwitchInputMethodEvent &event) {
            reset(event.inputContext(), true);
            onSurroundingText(event.inputContext());
          }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextKeyEvent>(
          fcitx::EventWatcherPhase::PreInputMethod,
          [this](fcitx::KeyEvent &event) { onKeyPre(event); }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextKeyEvent>(
          fcitx::EventWatcherPhase::PostInputMethod,
          [this](fcitx::KeyEvent &event) { onKey(event); }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextUpdateUI>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::InputContextUpdateUIEvent &event) {
            if (event.component() ==
                fcitx::UserInterfaceComponent::InputPanel) {
              display(property(event.inputContext()));
            }
          }));

  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextSurroundingTextUpdated>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::SurroundingTextUpdatedEvent &event) {
            onSurroundingText(event.inputContext());
          }));
  eventHandlers_.emplace_back(
      instance_->watchEvent<fcitx::EventType::InputContextCursorRectChanged>(
          fcitx::EventWatcherPhase::Default,
          [this](fcitx::CursorRectChangedEvent &event) {
            auto *context = event.inputContext();
            // Without a text snapshot a mouse move cannot be reconstructed.
            if (!context->surroundingText().isValid()) {
              auto *state = property(context);
              state->cursorText.awaitCursorUpdate();
              hideSentence(state);
            }
          }));

  reloadConfig();
  FCITX_INFO() << "Bilingual sentence translation addon loaded";
}

BilingualContextAddon::~BilingualContextAddon() { client_.reset(); }

void BilingualContextAddon::reloadConfig() {
  fcitx::readAsIni(config_, "conf/bilingualcontext.conf");
  applyConfig();
}

void BilingualContextAddon::setConfig(const fcitx::RawConfig &config) {
  config_.load(config, true);
  fcitx::safeSaveAsIni(config_, "conf/bilingualcontext.conf");
  applyConfig();
}

void BilingualContextAddon::applyConfig() {
  // Invalidate every old generation before changing the transport or switch.
  instance_->inputContextManager().foreach([this](fcitx::InputContext *context) {
    reset(context, false);
    return true;
  });
  client_.reset();
  if (*config_.enabled) {
    auto self = watch();
    client_ = std::make_unique<TranslationClient>(socketPath(*config_.socketPath),
        [this, self] {
          instance_->eventDispatcher().scheduleWithContext(
              self, [this] { consumeResponses(); });
        }, (*config_.timeoutSeconds + 5) * 1000);
  }
}

BilingualContextProperty *
BilingualContextAddon::property(fcitx::InputContext *context) {
  return context->propertyFor(&propertyFactory_);
}

bool BilingualContextAddon::isRime(fcitx::InputContext *context) const {
  return *config_.enabled && instance_->inputMethod(context) == "rime" &&
         !(context->capabilityFlags() & fcitx::CapabilityFlag::PasswordOrSensitive);
}

void BilingualContextAddon::onCommit(fcitx::CommitStringEvent &event) {
  auto *context = event.inputContext();
  FCITX_DEBUG() << "Sentence commit observed: bytes=" << event.text().size()
                << " im=" << instance_->inputMethod(context);
  if (!isRime(context) || event.text().empty()) {
    return;
  }
  auto *state = property(context);
  state->cursorText.commit(event.text());
  if (state->finishOnNextCommit) {
    state->finishOnNextCommit = false;
    finishAfterSpace(state);
    return;
  }
  if (state->cursorText.sentence(*config_.sentenceBoundaries).empty()) {
    // Translate the just-finished unit in the background for a later revisit.
    auto previous = state->cursorText.previousSentence(*config_.sentenceBoundaries);
    if (auto request = state->sentence.selectSentence(previous)) {
      request->final = true;
      submit(state->token, *request);
    }
  }
  refreshSentence(state, false);
}

void BilingualContextAddon::hideSentence(BilingualContextProperty *state) {
  state->sentence.selectSentence("");
  state->debounce.reset();
  clearDisplay(state);
}

void BilingualContextAddon::refreshSentence(BilingualContextProperty *state,
                                           bool immediate) {
  const auto text = state->cursorText.sentence(*config_.sentenceBoundaries);
  if (text == state->sentence.activeSentence() && state->sentence.valid()) {
    return;
  }
  auto request = state->sentence.selectSentence(text);
  state->debounce.reset();
  clearDisplay(state);
  if (request) {
    if (immediate) {
      submit(state->token, *request);
    } else {
      schedule(state, *request);
    }
  } else if (!state->sentence.translation().empty()) {
    display(state);
    if (auto *context = state->context.get()) {
      context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel, true);
    }
  }
}

void BilingualContextAddon::onSurroundingText(fcitx::InputContext *context) {
  if (!context->hasFocus() || !isRime(context)) {
    return;
  }
  auto *state = property(context);
  const auto &surrounding = context->surroundingText();
  FCITX_DEBUG() << "Sentence cursor snapshot: valid=" << surrounding.isValid()
                << " cursor=" << surrounding.cursor()
                << " anchor=" << surrounding.anchor();
  if (!surrounding.isValid()) {
    state->cursorText.awaitCursorUpdate();
    hideSentence(state);
    return;
  }
  if (state->cursorText.update(surrounding.text(), surrounding.cursor(),
                               surrounding.anchor())) {
    refreshSentence(state, true);
  }
}

void BilingualContextAddon::onKeyPre(fcitx::KeyEvent &event) {
  if (event.isRelease() || !event.key().check(FcitxKey_space) ||
      !SentenceState::isBoundary(" ", *config_.sentenceBoundaries) ||
      !isRime(event.inputContext())) {
    return;
  }

  auto *state = property(event.inputContext());
  hideSentence(state);
  state->finishOnNextCommit = true;
  const auto sequence = ++state->spaceSequence;
  const auto contextReference = state->context;
  auto self = watch();
  instance_->eventDispatcher().scheduleWithContext(
      self, [this, contextReference, sequence] {
        auto *inputContext = contextReference.get();
        if (!inputContext) {
          return;
        }
        auto *current = property(inputContext);
        if (current->spaceSequence == sequence &&
            current->finishOnNextCommit) {
          current->finishOnNextCommit = false;
          finishAfterSpace(current);
        }
      });
}

void BilingualContextAddon::onKey(fcitx::KeyEvent &event) {
  if (event.isRelease()) {
    return;
  }
  auto *context = event.inputContext();
  if (!isRime(context) || event.filtered()) {
    return;
  }
  auto *state = property(context);
  const auto key = event.key().sym();
  if (key == FcitxKey_Left || key == FcitxKey_Right ||
      key == FcitxKey_Up || key == FcitxKey_Down ||
      key == FcitxKey_Home || key == FcitxKey_End ||
      key == FcitxKey_Page_Up || key == FcitxKey_Page_Down ||
      key == FcitxKey_Return || key == FcitxKey_KP_Enter ||
      event.key().states().test(fcitx::KeyState::Ctrl) ||
      event.key().states().test(fcitx::KeyState::Alt)) {
    state->cursorText.awaitCursorUpdate();
    hideSentence(state);
    return;
  }
  if (event.key().check(FcitxKey_BackSpace) ||
      event.key().check(FcitxKey_Delete)) {
    state->cursorText.erase(event.key().check(FcitxKey_Delete));
    refreshSentence(state, false);
  }
}

void BilingualContextAddon::finishAfterSpace(
    BilingualContextProperty *state) {
  auto text = state->cursorText.sentence(*config_.sentenceBoundaries);
  if (text.empty()) {
    text = state->cursorText.previousSentence(*config_.sentenceBoundaries);
  }
  if (auto request = state->sentence.selectSentence(text)) {
    request->final = true;
    submit(state->token, *request);
  }
  state->cursorText.finish();
  hideSentence(state);
}

void BilingualContextAddon::reset(fcitx::InputContext *context, bool valid) {
  FCITX_DEBUG() << "Reset sentence state: valid=" << valid;
  auto *state = property(context);
  state->finishOnNextCommit = false;
  ++state->spaceSequence;
  state->debounce.reset();
  state->sentence.reset(valid);
  state->cursorText.reset();
  clearDisplay(state);
}

void BilingualContextAddon::schedule(BilingualContextProperty *state,
                                     const SentenceRequest &request) {
  auto context = state->context;
  const auto token = state->token;
  state->debounce = instance_->eventLoop().addTimeEvent(
      CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + static_cast<std::uint64_t>(*config_.debounceMs) * 1000, 0,
      [this, context, token, request](fcitx::EventSourceTime *, std::uint64_t) {
        auto *inputContext = context.get();
        if (!inputContext || !inputContext->hasFocus() ||
            !isRime(inputContext)) {
          return false;
        }
        auto *current = property(inputContext);
        if (current->token == token && current->sentence.valid() &&
            current->sentence.generation() == request.generation) {
          FCITX_DEBUG() << "Debounce fired: generation=" << request.generation;
          submit(token, request);
        }
        return false;
      });
}

void BilingualContextAddon::submit(const std::string &context,
                                   const SentenceRequest &request) {
  FCITX_DEBUG() << "Queue translation: generation=" << request.generation
                << " bytes=" << request.text.size();
  if (!client_ || !*config_.enabled) {
    return;
  }
  client_->submit({nextRequest_++, context, request.generation, request.text,
                   request.final});
}

void BilingualContextAddon::consumeResponses() {
  if (!client_) {
    return;
  }
  for (auto &response : client_->takeResponses()) {
    FCITX_DEBUG() << "Translation response: generation=" << response.generation
                  << " bytes=" << response.translation.size();
    instance_->inputContextManager().foreach (
        [this, &response](fcitx::InputContext *context) {
          auto *state = property(context);
          if (state->token != response.context) {
            return true;
          }
          if (context->hasFocus() && isRime(context) &&
              state->sentence.acceptTranslation(
                  response.generation, std::move(response.translation))) {
            display(state);
            context->updateUserInterface(
                fcitx::UserInterfaceComponent::InputPanel, true);
          }
          return false;
        });
  }
}

void BilingualContextAddon::display(BilingualContextProperty *state) {
  auto *context = state->context.get();
  if (!context || !context->hasFocus() || !isRime(context) ||
      state->sentence.translation().empty()) {
    return;
  }
  const auto hint = std::string(kHintPrefix) + state->sentence.translation();
  auto current = context->inputPanel().auxDown().toString();
  if (current == hint ||
      (!state->injectedAuxDown.empty() && current == state->injectedAuxDown)) {
    return;
  }
  state->originalAuxDown = current;
  if (!current.empty()) {
    current += "\n";
  }
  current += hint;
  context->inputPanel().setAuxDown(fcitx::Text(current));
  state->injectedAuxDown = std::move(current);
}

void BilingualContextAddon::clearDisplay(BilingualContextProperty *state) {
  auto *context = state->context.get();
  const bool owned = context && !state->injectedAuxDown.empty() &&
      context->inputPanel().auxDown().toString() == state->injectedAuxDown;
  auto original = std::move(state->originalAuxDown);
  state->injectedAuxDown.clear();
  state->originalAuxDown.clear();
  if (owned) {
    context->inputPanel().setAuxDown(fcitx::Text(original));
    context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
  }
}

class BilingualContextFactory final : public fcitx::AddonFactory {
public:
  fcitx::AddonInstance *create(fcitx::AddonManager *manager) override {
    return new BilingualContextAddon(manager->instance());
  }
};

} // namespace bilingual

FCITX_ADDON_FACTORY_V2(bilingualcontext, bilingual::BilingualContextFactory);
