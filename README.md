<p align="center">
  <img src="assets/yiban-logo.png" alt="Yiban IME logo" width="180" />
</p>

<h1 align="center">Yiban IME · 译伴输入法</h1>

<p align="center"><strong>English</strong> | <a href="README.zh-CN.md">简体中文</a></p>

**Learn English while typing Chinese.**

Yiban IME is a Linux input method built on **Fcitx5 + Rime**, designed to help English learners connect familiar Chinese words and sentences with English meanings and expressions during everyday typing.

Chinese candidates show local English definitions. Optional sentence translations appear below the candidate list and follow the text under your cursor. Your application receives the Chinese text you selected.

## Features

- **English word hints:** local CC-CEDICT definitions alongside Chinese candidates, with no model or network required.
- **Sentence translation:** use a local or remote Ollama server; enable or disable it independently.
- **Cursor-aware hints:** Space ends the current unit and hides the hint. Returning to an earlier sentence restores its cached translation; editing updates it.
- **Familiar Chinese input:** Rime handles Pinyin and learning, with optional Rime Ice vocabulary.

The input-method list displays **Yiban**. Its internal ID remains `rime` to preserve existing profiles and learning data. The supplied profile contains only **English (US)** and **Yiban**.

## Install

Requires Linux, a C++20 compiler, CMake ≥ 3.20, Python ≥ 3.10, Fcitx5 Core/Config ≥ 5.1.22 development files, and nlohmann-json ≥ 3.11.

```sh
git clone https://github.com/billylu24/Yiban-IME.git
cd Yiban-IME

# Use a prefix and library directory compatible with your running Fcitx.
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Follow the [deployment guide](docs/deployment.md) to install into your system or a custom prefix, apply the Rime word-hint patch, and set up the translation service. Private-prefix installations also need the user icon installation step so desktop panels can find the logo.

## Configure

Settings live in `~/.config/fcitx5/conf/bilingualcontext.conf` by default; XDG configuration paths are supported. See the [complete example](config/bilingualcontext.conf).

```ini
Enabled=True
OllamaUrl=http://127.0.0.1:11434
OllamaModel=qwen3.5:0.8b
DebounceMs=200
```

Set `Enabled=False` to keep Chinese input and English word hints without sentence translation. After changing settings:

```sh
systemctl --user restart bilingual-ime-translator.service
fcitx5-remote -r
```

The model must already be available on the configured Ollama server. The backend uses Ollama's `/api/generate`, not an OpenAI-compatible API. Remote servers receive the sentences being translated.

## Notes

- Translation is a learning aid: model output can be inaccurate, especially for names, terminology, and long sentences.
- Restoring translations after cursor movement depends on the application's surrounding-text support.
- Tested on Linux x86_64; compatibility varies across distributions, desktop environments, and applications.

[Installation, migration, tests and troubleshooting →](docs/deployment.md)

## Credits and licensing

Built with Fcitx5, Rime, CC-CEDICT, optional Rime Ice vocabulary, and Ollama. CC-CEDICT data is licensed under [CC BY-SA 4.0](data/dictionary/CC-CEDICT-LICENSE.txt); other components retain their own licenses. The project logo was supplied by the maintainer. A license for this repository's own source code has not yet been selected.
