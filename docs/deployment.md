# Deployment guide

[Project overview](../README.md) | [简体中文](deployment.zh-CN.md)

## Requirements

| Purpose | Dependencies |
|---|---|
| Core libraries | CMake ≥ 3.20, C++20 compiler, Python ≥ 3.10, threads |
| Sentence addon | Fcitx5 Core/Config ≥ 5.1.22 development files, nlohmann-json ≥ 3.11 |
| Chinese input | Fcitx5, fcitx5-rime, librime, and a working Rime schema |
| Default translation | Python ≥ 3.10, reachable Ollama server, downloaded model |
| Automatic service startup | systemd user services; manual execution is also supported |
| Cursor integration tests | `gjs`, `dbus-run-session`, and the project-local Fcitx test prefix |
| Optional Argos backend | `ctranslate2`, `sentencepiece`, and a compatible model directory |

The Ollama daemon uses Python's standard library only. Conda and pip dependencies are unnecessary for that backend. Inference runs on the Ollama server; the input machine can use a remote server.

Validated upstream source baseline:

| Project | Version | Commit |
|---|---|---|
| Fcitx5 | 5.1.22 | `cdd0b9d900770d1ad1229d759213215d5dc23a90` |
| fcitx5-rime | 5.1.14 | `3509646289ec88f5c6c3956b343c305275aa8d3b` |
| librime | 1.17.0 | `13faefe2819d01fce208752c2539744094bb4787` |

These are tested source revisions, not a claim that every distribution ships matching packages. See the [source research](../docs/research.md) in Chinese.

## Build and install

Start with working Fcitx/Rime Chinese input. Run the following commands from the repository root:

```sh
git clone https://github.com/billylu24/Yiban-IME.git
cd Yiban-IME
```

### Core libraries only

This does not require Fcitx development packages:

```sh
cmake -S . -B build-core \
  -DCMAKE_BUILD_TYPE=Release \
  -DBILINGUAL_BUILD_ADDON=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

The bundled CC-CEDICT archive is converted at build time without downloading data. IPC libraries and protocol tests are included when nlohmann-json is found.

### Full addon with system Fcitx

`BILINGUAL_BUILD_ADDON=ON` is the default. Missing required dependencies cause a configuration error rather than silently omitting translation.

Use the **running Fcitx installation's library directory**:

```sh
pkg-config --variable=libdir Fcitx5Core
```

For example, when its addons are in `/usr/lib/x86_64-linux-gnu/fcitx5`:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# Exit Fcitx before replacing its loaded libraries.
fcitx5-remote -e
sudo cmake --install build
fcitx5 -d
```

The multiarch path above is an example, not a portable constant. Use your platform's actual `lib`, `lib64`, or multiarch directory.

The sentence addon works with a compatible ordinary fcitx5-rime installation. Candidate word hints require the patch below.

### Patched Rime and a custom prefix

This example assumes compatible Fcitx/librime dependencies are already available in your chosen prefix:

```sh
YIBAN_PREFIX="$HOME/.local/opt/yiban"

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$YIBAN_PREFIX" \
  -DCMAKE_PREFIX_PATH="$YIBAN_PREFIX"
cmake --build build --parallel
cmake --install build

mkdir -p upstream
git clone https://github.com/fcitx/fcitx5-rime.git upstream/fcitx5-rime
git -C upstream/fcitx5-rime checkout 3509646289ec88f5c6c3956b343c305275aa8d3b
git -C upstream/fcitx5-rime apply "$PWD/patches/fcitx5-rime-word-hints.patch"
git -C upstream/fcitx5-rime apply "$PWD/patches/fcitx5-rime-yiban-branding.patch"

cmake -S upstream/fcitx5-rime -B build-rime \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$YIBAN_PREFIX" \
  -DCMAKE_PREFIX_PATH="$YIBAN_PREFIX"
cmake --build build-rime --parallel
cmake --install build-rime
```

Make sure that Fcitx in this prefix finds its own addons and data. Do not mix incompatible system and private libraries. Install upstream build dependencies according to your distribution and upstream documentation; this repository does not bootstrap every desktop component automatically.

The word-hint patch embeds the dictionary path at build time. Reconfigure and rebuild patched Rime after changing the prefix.

### Name and icon

`YIBAN_BRAND_RIME=ON` installs the **Yiban** input-method entry and PNG icons from 16px to 512px. The internal ID remains `rime`; no learning-data migration is required.

The branding patch also changes the engine's Chinese-mode icon. Existing Latin/disabled state icons remain available. Desktop panels control final icon rendering; Fcitx Classic UI can prefer images with `PreferTextIcon=False`.

**Private-prefix installations need one additional desktop integration step.**
The desktop shell does not inherit the Fcitx launcher's `XDG_DATA_DIRS`. Even when
Fcitx reports `fcitx-yiban`, the panel cannot render it unless the desktop can find
the image. Install the icons into the standard per-user icon directory:

```sh
scripts/install-user-icons.sh "$YIBAN_PREFIX"
# Without a checkout:
# "$YIBAN_PREFIX/share/yiban-ime/scripts/install-user-icons.sh" "$YIBAN_PREFIX"
```

This copies only Yiban's icons into `${XDG_DATA_HOME:-~/.local/share}/icons/hicolor`
and refreshes the icon cache. Restart Fcitx to refresh its tray item. System
installations under a desktop-visible `/usr/share/icons` normally do not need
this extra copy. Repeat the step after changing the logo or moving machines.

To keep the original Rime appearance, configure with `-DYIBAN_BRAND_RIME=OFF` and skip the branding patch. This option does not remove files from a previous installation; restore the original input-method descriptor when reverting branding.

### Existing project-local development environment

If `.local-env/prefix` and `.local-env/sysroot` already exist:

```sh
cmake -S . -B .local-env/build/bilingual \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/.local-env/prefix" \
  -DCMAKE_PREFIX_PATH="$PWD/.local-env/prefix;$PWD/.local-env/sysroot/usr"
cmake --build .local-env/build/bilingual --parallel
ctest --test-dir .local-env/build/bilingual --output-on-failure
```

`.local-env` is not included in Git. Rebuild dependencies on a new machine instead of copying it across distributions or CPU architectures.

The legacy `install-main-user-test.sh` / `restore-main-user-test.sh` scripts target the existing Debian/Ubuntu x86_64 development environment and modify the input-method profile. They are not general-purpose installers.

## Configuration

The addon and daemon share:

```text
${XDG_CONFIG_HOME:-~/.config}/fcitx5/conf/bilingualcontext.conf
```

When `FCITX_CONFIG_HOME` is set, use `conf/bilingualcontext.conf` below that directory. The daemon's `--config` changes only its own path; keep it consistent with Fcitx.

Copy [config/bilingualcontext.conf](../config/bilingualcontext.conf) on first installation. Preserve an existing configuration during upgrades.

```ini
Enabled=True
SocketPath=
DebounceMs=200
Backend=ollama
OllamaUrl=http://127.0.0.1:11434
OllamaModel=qwen3.5:0.8b
KeepAlive=5m
TimeoutSeconds=30
Warmup=True
ArgosModelPath=

[SentenceBoundaries]
0=space
1=.
2=!
3=?
4=。
5=！
6=？
```

| Setting | Meaning |
|---|---|
| `Enabled` | `False` disables sentence translation; Chinese input and word hints remain usable |
| `SocketPath` | Local addon-to-daemon Unix socket; empty means automatic |
| `DebounceMs` | Delay after committing text, 0–5000ms |
| `Backend` | `ollama` or optional `argos` |
| `OllamaUrl` | Ollama base URL, such as `http://127.0.0.1:11434` |
| `OllamaModel` | Server-side model name, default `qwen3.5:0.8b` |
| `KeepAlive` | Model idle residency, for example `5m`; managed by Ollama |
| `TimeoutSeconds` | Model timeout, 1–300 seconds; the addon allows an extra 5 seconds |
| `Warmup` | Prewarm the model when the service starts |
| `ArgosModelPath` | Compatible Argos/OPUS model directory; use an absolute path |
| `SentenceBoundaries` | One character per entry; `space` also enables Space-selection boundaries |

The default socket is `$XDG_RUNTIME_DIR/bilingual-ime/translator.sock`, falling back to `/tmp/bilingual-ime-UID/translator.sock` on both sides.

Custom sockets must use an absolute path and a **dedicated parent directory**. The daemon sets that directory to `0700` and the socket to `0600`; do not place the socket directly under `/tmp` or your home directory.

### Disable or reload

Set `Enabled=False`, then:

```sh
systemctl --user restart bilingual-ime-translator.service
fcitx5-remote -r
```

The disabled daemon exits successfully without loading a model. `Restart=on-failure` prevents a restart loop. Set `True` and repeat to re-enable it.

The Fcitx configuration tool can edit addon settings. Changes to the daemon's model, URL, or other settings still require a service restart. Reload both sides after changing the socket or timeout.

### Remote Ollama

```ini
OllamaUrl=http://192.168.1.20:11434
OllamaModel=qwen3.5:0.8b
```

The input method still connects to the local daemon. Only model requests go to the remote host, which must be reachable and have the model installed. Sentence text is sent to that server.

The backend calls Ollama **`/api/generate`**, not an OpenAI-compatible `/v1` API. API keys and custom authentication headers are not built in; authenticated deployments need an appropriate controlled proxy or tunnel.

### Overrides and older installations

Daemon precedence: **command line > environment > configuration file > defaults**. `Enabled` is controlled by the file. Supported legacy environment variables:

```text
BILINGUAL_TRANSLATOR_SOCKET
BILINGUAL_TRANSLATOR_BACKEND
BILINGUAL_TRANSLATOR_OLLAMA_URL
BILINGUAL_TRANSLATOR_OLLAMA_MODEL
BILINGUAL_TRANSLATOR_OLLAMA_KEEP_ALIVE
BILINGUAL_TRANSLATOR_OLLAMA_TIMEOUT
BILINGUAL_TRANSLATOR_ARGOS_MODEL
BILINGUAL_TRANSLATOR_PYTHON
```

The socket variable also overrides the addon. The Python variable is interpreted by launcher scripts.

Old systemd units or drop-ins may contain `Environment=` overrides. Replace the unit using the service installer and inspect `systemctl --user cat bilingual-ime-translator.service`. Prefer a single configuration file for normal use.

## Translation service

### Prepare the server and model

Install and start [Ollama](https://ollama.com/), or use an existing remote server.

```sh
python3 translator/daemon.py --config config/bilingualcontext.conf --check-config
# Reads the user configuration and pulls on that server, not necessarily localhost.
scripts/setup-translator.sh
```

The setup script calls `/api/pull` over HTTP, so an Ollama CLI is not required on the input machine. Model downloads can take time. You can also provision models separately with Ollama's tools.

### Run without systemd

```sh
# From a checkout:
python3 translator/daemon.py

# From installed files:
/path/to/prefix/bin/yiban-translator
```

The socket opens after startup warmup. `ready on ...` indicates the daemon is listening. Other process managers can run the same command.

### Install a user service

CMake installs the daemon, launcher, configuration example, and service installer. A checkout is not needed at runtime. With the normal `bin` / `share` layout:

```sh
scripts/install-translator-service.sh /path/to/prefix

# Or, without the checkout:
/path/to/prefix/share/yiban-ime/scripts/install-translator-service.sh /path/to/prefix
```

The installer creates configuration only if missing, validates it, backs up an existing service unit, and enables/restarts the user service. It does not download a model, change your Rime profile, or restart Fcitx.

Use `--no-start` to generate files without operating systemd.

```sh
systemctl --user status bilingual-ime-translator.service
journalctl --user -u bilingual-ime-translator.service -n 50 --no-pager
```

This is a per-user login service, not a shared system-wide translation service.

## Rime Ice dictionary

Rime Ice provides Chinese vocabulary; CC-CEDICT provides English definitions. They are independent. Rime Ice is not required for sentence translation.

```sh
# Existing project-local environment:
scripts/update-rime-ice-dictionary.sh

# Example system installation; use actual paths on your distribution:
YIBAN_FCITX_PREFIX=/usr \
RIME_DEPLOYER=/usr/bin/rime_deployer \
RIME_SHARED_DIR=/usr/share/rime-data \
scripts/update-rime-ice-dictionary.sh
```

Custom library directories can be supplied through `YIBAN_EXTRA_LIBRARY_PATH`.

The script updates user dictionary files and **replaces `pinyin_simp.custom.yaml`** to select `rime_ice`. Back up and merge personal customization first. It does not delete learning databases.

By default it follows upstream `main`. Pin a revision for repeatable deployment:

```sh
RIME_ICE_REF=<actual-tag-or-commit> scripts/update-rime-ice-dictionary.sh
```

The installed commit is recorded in `rime-ice.version` inside the user Rime directory. Back up dictionaries and custom configuration before upgrading.

## Moving to another machine

1. Install compatible Fcitx/Rime and build dependencies on the target.
2. Check out the same project revision; build the addon and optional patches for that system's ABI and library paths.
3. Copy the user configuration and review server addresses, model names, and absolute paths.
4. Generate a new systemd unit with the installer; do not copy an old absolute-path unit.
5. Deploy the recorded Rime Ice revision.
6. Use Rime's synchronization/backup facilities for personal learning data, preserving the destination's data first.
7. Run configuration checks, tests, and actual application input/cursor checks.

Do not migrate build directories, `.local-env/sysroot`, old `.so` files, sockets, PID/lock files, Python bytecode, or logs.

The standalone Python service can move with a consistent `bin` / `share` layout; regenerate its systemd unit afterward. Rebuild C++ addons, Fcitx, and patched Rime for the new prefix and ABI.

## Updating and uninstalling

Back up configuration, rebuild, test, exit Fcitx, install, and restart. Do not overwrite shared libraries while they are mapped by a running process.

```sh
systemctl --user disable --now bilingual-ime-translator.service
rm "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/bilingual-ime-translator.service"
systemctl --user daemon-reload
```

Also set `Enabled=False` and reload Fcitx when disabling the service.

CMake has no automatic uninstall target. Remove packaged installations through the package manager. For manual installs, review `install_manifest.txt` before removing individual files. Preserve shared Fcitx/Rime dependencies, personal configuration, and learning data. Restore the original Rime descriptor/icon when removing Yiban branding.

For the legacy main-user test installation, use its paired `scripts/restore-main-user-test.sh`.

## Testing and troubleshooting

```sh
ctest --test-dir build --output-on-failure
```

Tests cover dictionary parsing, sentence/cursor state, caches, IPC frames, configuration, and Ollama request construction. Mock/fake model responses do not establish real translation quality.

With the project-local test prefix:

```sh
scripts/test-cursor-translation.sh /path/to/addon-build
YIBAN_TEST_DISABLED=1 scripts/test-cursor-translation.sh /path/to/addon-build
```

These use an isolated D-Bus session, temporary configuration, and a fake daemon without restarting the desktop input method. Coverage includes Space hiding, cached return, edits, real Rime candidate selection, and the disabled switch.

Dictionary benchmark:

```sh
cmake -S . -B build-core -DBILINGUAL_BUILD_ADDON=OFF -DBILINGUAL_BUILD_BENCHMARKS=ON
cmake --build build-core --parallel
./build-core/dictionary_benchmark build-core/dictionary/base.tsv
```

This measures lookup, not candidate-page segmentation or end-to-end model latency.

| Symptom | Check |
|---|---|
| Fcitx5Core not found | Development package version and `CMAKE_PREFIX_PATH` |
| Addon not found after installation | The running Fcitx's library/data search paths |
| No sentence hint | `Enabled`, active input method, daemon logs, model availability |
| Configuration changes ignored | Reload addon, restart daemon, inspect environment/drop-ins |
| Socket connection fails | Matching paths, path length, ownership and permissions |
| Returning to a sentence is not instant | Cache availability and application surrounding-text support |
| Missing word hints | Patched Rime and its embedded dictionary path |
| Old name or icon remains | Descriptor installation order, branding patch, icon cache and desktop theme |
| Slow first translation | Warmup, hardware, network, model residency |
| Reachable URL but failed generation | Endpoint must implement Ollama `/api/generate` |

## Project layout

```text
assets/             Original project logo
src/addon/          Fcitx events, configuration, UI, timers
src/context/        Sentence state, cursor prediction, translation cache
src/dictionary/     Read-only bilingual dictionary
src/translation/    C++ IPC protocol and worker client
translator/         Python daemon and model backends
config/             Shared configuration and Rime examples
patches/            Word-hint and branding patches for pinned upstream Rime
data/               Dictionary/license, input-method descriptor, icon sizes
scripts/            Installation, dictionary updates, development and tests
tests/              Unit and integration tests
benchmarks/         Dictionary lookup benchmark
docs/               Research, historical design, release guidance
```

The [architecture document](../docs/architecture.md) contains historical plans, including mechanisms not yet implemented. This README and the code describe current behavior.

## Known limitations

- Returning to old text and external editing depend on application-provided surrounding text; toolkit, browser, and terminal support varies.
- Translation caches hold the latest 32 exact-text entries per input context and are not persisted.
- Cursor-mode translation skips units larger than 4096 bytes.
- Requests are handled serially; slow inference can delay newer requests. Inference cancellation is not implemented.
- Small models can mistranslate names, negation, terminology, and long sentences.
- Output is currently limited to 256 tokens; long translations may be truncated. Automatic chunking and comprehensive quality evaluation are not implemented.
- Normal logs use lengths and request IDs rather than complete input. Review exception logs before sharing them.
- Password/sensitive input contexts are excluded. Remote Ollama receives the text being translated.
- No native Windows/macOS IME or maintained deb/rpm/AUR packages are currently provided.

