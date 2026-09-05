<p align="center">
  <img src="assets/yiban-logo.png" alt="Yiban IME 标识" width="200" />
</p>

# Yiban IME · 译伴输入法

[English](README.md) | **简体中文**

**用熟悉的拼音输入中文，让英文释义与翻译随输入相伴。**

Yiban IME 是面向 Linux / Fcitx5 / Rime 的双语输入增强项目：候选词旁显示本地英文释义，光标所在的中文句子可通过 Ollama 翻译为英文，显示在候选框下方。

项目保留 Rime 的中文输入、候选选择与用户学习能力。英文是辅助提示，不会替换你提交给应用的中文。整句翻译可以独立关闭；关闭后不需要 Ollama。

> 当前定位：可试用的早期版本，已在开发机 Linux x86_64 环境验证。尚未完成多发行版、ARM 和完整 GTK/Qt/浏览器兼容矩阵，不应当作已支持所有 Linux 桌面的通用二进制包。

## 目录

- [功能与使用行为](#功能与使用行为)
- [项目各部分的关系](#项目各部分的关系)
- [依赖与支持范围](#依赖与支持范围)
- [构建与安装](#构建与安装)
- [统一配置](#统一配置)
- [翻译服务部署](#翻译服务部署)
- [雾凇词库](#雾凇词库)
- [迁移到另一台机器](#迁移到另一台机器)
- [更新与卸载](#更新与卸载)
- [测试与排错](#测试与排错)
- [项目结构](#项目结构)
- [已知限制](#已知限制)
- [许可证与发布](#许可证与发布)

## 功能与使用行为

### 候选词英文释义

- 根据 CC-CEDICT 生成本地词典，覆盖简体与繁体词条。
- 启动时加载到内存；输入过程中不访问网络或模型。
- 完整词未命中时，尝试可以覆盖整个词的分段匹配。
- 保留 Rime 原有候选 comment，并附加英文释义。
- 词典缺失或损坏时关闭释义，仍允许 Rime 启动。

分段释义用于理解词义，并不保证拼接后是一句自然英文。

### 随光标显示的整句翻译

| 操作 | 英文提示行为 |
|---|---|
| 提交中文后停顿 | 默认等待 200ms，再请求当前句子的翻译 |
| 按空格，包括空格选词 | 结束当前翻译单元，立即隐藏提示 |
| 输入 `。！？.!?` 后进入空白单元 | 隐藏上一句，等待新内容 |
| 下一句开始有内容 | 翻译新句 |
| 光标返回上一句 | 有精确文本缓存时立即恢复；否则请求翻译 |
| 在上一句插入或删除 | 按实际光标位置更新整句，保留光标后的文字 |
| 删除两句之间的断句符 | 可以合并相邻句子并更新翻译 |
| 选区非空、焦点离开或未知光标位置 | 隐藏提示 |
| 旧请求在光标离开后返回 | 不重新弹出旧提示；结果可进入缓存 |

200ms 是发送请求前的防抖时间，不是模型完成翻译的时间。只有已经提交的文字参与翻译，尚未选中的拼音和候选不参与。

“句子”在本项目中指由断句符划分的翻译单元，不是模型判断的语法完整句。逗号、顿号、分号和冒号默认不断句；换行会分隔光标所在单元。

空格选词即使没有真正插入空格，也会留下本地单元边界。该边界属于当前输入上下文；外部改写文本或重置上下文后，以应用提供的实际文本为准。

## 项目各部分的关系

```text
应用 ← 中文提交 ← Fcitx5 + Rime ← 拼音按键
                       │
                       ├─ 雾凇词库：中文词条、读音和词频
                       ├─ CC-CEDICT：候选英文释义
                       └─ bilingualcontext：跟踪当前句子与光标
                                      │ 本机 Unix socket
                               translator daemon
                                      │ HTTP(S)
                              本机或远程 Ollama
                                      │
                              EN: 英文辅助提示
```

| 名称 | 作用 |
|---|---|
| Fcitx5 | 输入框接入、按键路由和候选界面 |
| Rime / librime | 拼音解析、候选生成、选词和用户学习 |
| `pinyin_simp` | 当前使用的 Rime 输入方案 |
| 雾凇 `rime_ice` | 可选的中文主词典替换 |
| `bilingualcontext` | 本项目的整句翻译 addon |
| translator daemon | 独立进程，调用翻译后端 |

对外项目名为 **译伴 / Yiban IME**。为了兼容已有安装，内部继续使用 `rime`、`bilingualcontext`、`bilingual-ime-translator.service` 等标识。Fcitx 输入法列表中显示 **Yiban**，使用本仓库的标识图；内部 ID 仍为 `rime`，保留原有 profile、schema 与学习数据兼容性。addon 配置页显示「译伴整句翻译」。

## 依赖与支持范围

| 用途 | 依赖 |
|---|---|
| 构建核心库 | CMake ≥ 3.20、支持 C++20 的编译器、Python ≥ 3.10、线程库 |
| 构建整句 addon | Fcitx5 Core/Config ≥ 5.1.22 开发包、nlohmann-json ≥ 3.11 |
| 中文输入 | Fcitx5、fcitx5-rime、librime、可用的 Rime schema |
| 默认整句翻译 | Python ≥ 3.10、可访问的 Ollama、已下载的模型 |
| 自动启动翻译服务 | systemd 用户服务；非 systemd 环境可手动运行 |
| 光标集成测试 | `gjs`、`dbus-run-session`、项目内 Fcitx 测试 prefix |
| 可选 Argos 后端 | `ctranslate2`、`sentencepiece`、兼容的模型目录 |

Ollama 后端的 Python daemon 仅使用标准库，不需要 Conda 或 pip 安装依赖。模型推理在 Ollama 所在机器上进行，本机可以只运行输入法与 Python daemon。

已验证的上游源码基线：

| 项目 | 版本 | 固定 commit |
|---|---|---|
| Fcitx5 | 5.1.22 | `cdd0b9d900770d1ad1229d759213215d5dc23a90` |
| fcitx5-rime | 5.1.14 | `3509646289ec88f5c6c3956b343c305275aa8d3b` |
| librime | 1.17.0 | `13faefe2819d01fce208752c2539744094bb4787` |

这些是验证过的源码版本，不代表所有发行版都已提供同版本软件包。详细调研见 [docs/research.md](docs/research.md)。

## 构建与安装

仓库地址：[billylu24/Yiban-IME](https://github.com/billylu24/Yiban-IME)。

下面命令从本仓库根目录执行。首次使用请先确保 Fcitx/Rime 本身能正常输入中文，再添加增强功能。

### 1. 只构建核心库

不需要 Fcitx 开发包，适合测试词典或构建词级提示依赖：

```sh
cmake -S . -B build-core \
  -DCMAKE_BUILD_TYPE=Release \
  -DBILINGUAL_BUILD_ADDON=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

CC-CEDICT 压缩源随仓库提供，构建时自动转换，不在构建阶段下载词典。如果找到了 nlohmann-json，也会构建 IPC 库与协议测试。

### 2. 构建完整 addon

默认 `BILINGUAL_BUILD_ADDON=ON`。缺少 Fcitx 或 JSON 开发依赖时会明确失败，不会静默生成一个没有整句翻译功能的安装包。

**使用系统 Fcitx 时**，安装位置必须与系统 Fcitx 的 addon 搜索目录一致。可先查看：

```sh
pkg-config --variable=libdir Fcitx5Core
```

例如系统 addon 位于 `/usr/lib/x86_64-linux-gnu/fcitx5` 时：

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

`lib/x86_64-linux-gnu` 只是该系统的示例。其他平台应使用它自己的 `lib`、`lib64` 或 multiarch 目录，不要照抄架构名。安装前退出 Fcitx，安装后重新启动：

```sh
fcitx5-remote -e
sudo cmake --install build
fcitx5 -d
```

整句 addon 可与兼容的普通 fcitx5-rime 配合使用；只有“候选词英文释义”需要下面的 Rime 补丁。

### 3. 给 fcitx5-rime 添加候选释义

先将本项目词典库安装到目标 prefix，再获取并构建固定版本的 fcitx5-rime。以下以已经准备好兼容 Fcitx/librime 的自定义 prefix 为例：

```sh
# 修改为你的实际安装目录；不要填源码目录。
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

自定义 prefix 中的 Fcitx 必须能够找到对应的插件与数据文件。不要把自定义 prefix 的插件随意混入版本不同的系统 Fcitx。上游 Fcitx/librime 的构建依赖需按目标发行版与上游文档安装；本仓库没有自动重建全部上游桌面组件的一键脚本。

Rime 补丁在编译时确定词典路径。更换 prefix 后应重新配置、编译并安装 patched Rime，不要仅复制旧 `.so`。

### Yiban 名称与图标

默认 `YIBAN_BRAND_RIME=ON`，安装输入法描述文件和多尺寸 PNG 图标，显示名称为 Yiban。内部仍是 `rime`，无需迁移用户词库。

若只想使用翻译插件并保留原 Rime 外观，构建时传入 `-DYIBAN_BRAND_RIME=OFF`，并跳过 branding patch。恢复已有安装的原名称时，需要重新安装发行版的 Rime 输入法描述文件；仅关闭构建选项不会删除已经安装的文件。

branding patch 同时更新引擎返回的中文模式图标；英文/禁用模式保留原有状态图标。托盘是否显示图片由桌面与主题决定，Fcitx Classic UI 可将 `PreferTextIcon=False` 以优先使用图标。

### 4. 使用现有开发机的隔离环境

若已具备本项目的 `.local-env/prefix` 和 `.local-env/sysroot`：

```sh
cmake -S . -B .local-env/build/bilingual \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/.local-env/prefix" \
  -DCMAKE_PREFIX_PATH="$PWD/.local-env/prefix;$PWD/.local-env/sysroot/usr"
cmake --build .local-env/build/bilingual --parallel
ctest --test-dir .local-env/build/bilingual --output-on-failure
```

`.local-env` 是本机开发产物，不会随 Git 克隆提供。新机器应重建依赖；它不是可跨发行版、跨 CPU 直接复制的运行包。

旧的 `install-main-user-test.sh` / `restore-main-user-test.sh` 用于开发机临时切换，依赖现有隔离环境和 Debian/Ubuntu x86_64 路径，并会修改输入法 profile。它们不是通用发行版安装器。

## 统一配置

默认配置文件：

```text
${XDG_CONFIG_HOME:-~/.config}/fcitx5/conf/bilingualcontext.conf
```

如果设置了 `FCITX_CONFIG_HOME`，使用其下的 `conf/bilingualcontext.conf`。插件与 daemon 读取同一个文件。daemon 的 `--config /absolute/path` 只改变 daemon 的读取路径，使用时需保证与 Fcitx 的配置一致。

第一次创建配置时，复制 [config/bilingualcontext.conf](config/bilingualcontext.conf)。后续升级保留已有配置，不要覆盖个人设置：

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf"
# 仅在目标文件尚不存在时执行：
cp config/bilingualcontext.conf \
  "${XDG_CONFIG_HOME:-$HOME/.config}/fcitx5/conf/bilingualcontext.conf"
```

完整示例：

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

| 参数 | 含义 |
|---|---|
| `Enabled` | `False` 关闭整个句子翻译功能；仍可中文输入和查本地词义 |
| `SocketPath` | 插件到本机 daemon 的 Unix socket；留空自动选择 |
| `DebounceMs` | 中文提交后的防抖，范围 0–5000ms，默认 200 |
| `Backend` | `ollama` 或可选的 `argos` |
| `OllamaUrl` | Ollama **基础地址**，例如 `http://127.0.0.1:11434` |
| `OllamaModel` | 已安装的 Ollama 模型名称，默认 `qwen3.5:0.8b` |
| `KeepAlive` | 模型空闲驻留时间，如 `5m`；具体资源管理由 Ollama 决定 |
| `TimeoutSeconds` | 单次模型请求超时，范围 1–300 秒；插件等待额外留出 5 秒 |
| `Warmup` | 服务启动时是否请求一次短翻译预热模型 |
| `ArgosModelPath` | Argos/OPUS 兼容模型目录，仅该后端需要；应填写绝对路径 |
| `SentenceBoundaries` | 每项一个字符；`space` 表示空格，也启用空格选词结束单元 |

默认 socket 为 `$XDG_RUNTIME_DIR/bilingual-ime/translator.sock`。没有 runtime 目录时，两端都使用 `/tmp/bilingual-ime-UID/translator.sock`。

自定义 socket 必须使用绝对路径和专用父目录，例如 `/run/user/1000/yiban-custom/translator.sock`。daemon 会将父目录权限设为 `0700`、socket 设为 `0600`，不要直接把 `/tmp` 或用户主目录作为 socket 的父目录。

### 关闭整句翻译

把配置中的 `Enabled=True` 改成 `Enabled=False`，然后重新加载：

```sh
systemctl --user restart bilingual-ime-translator.service
fcitx5-remote -r
```

daemon 读取关闭设置后直接退出，不加载模型；用户服务使用 `Restart=on-failure`，不会因正常退出不断重启。Fcitx 配置工具中的「译伴整句翻译」也可修改插件参数，但 **daemon 参数仍需要重启服务才生效**。

重新开启时改回 `True`，重复上述命令。修改地址、模型、socket 或超时时，也建议同时重启服务并重新加载 Fcitx 配置。

### 使用另一台机器上的 Ollama

```ini
OllamaUrl=http://192.168.1.20:11434
OllamaModel=qwen3.5:0.8b
```

输入法仍连接本机 daemon；只有 daemon 到模型服务的 HTTP 请求走远程地址。远程服务需要已配置监听地址和访问控制，并安装对应模型。地址变化意味着待翻译句子会发送到该服务器。

当前实现调用 Ollama `/api/generate`，**不是 OpenAI-compatible `/v1` 接口**。没有内置 API key 或自定义认证头支持；需要认证时应由受控代理或隧道处理。

### 配置优先级与旧环境

daemon 参数优先级为：**命令行 > 环境变量 > 配置文件 > 默认值**。`Enabled` 只由配置文件控制。保留的环境变量包括：

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

`BILINGUAL_TRANSLATOR_SOCKET` 也会覆盖插件的 socket 设置。`BILINGUAL_TRANSLATOR_PYTHON` 仅供启动脚本选择 Python。

旧版 systemd 单元可能写有 `Environment=BILINGUAL_TRANSLATOR_...`，它们会覆盖新配置。升级时使用新的服务安装脚本替换单元，并检查 `systemctl --user cat bilingual-ime-translator.service` 中的 drop-in 是否仍有覆盖。推荐日常使用只维护配置文件。

## 翻译服务部署

### 1. 安装和检查 Ollama

按 [Ollama 官方说明](https://ollama.com/) 安装服务器并启动。可使用本机服务，也可使用已有远程服务。

```sh
python3 translator/daemon.py --config config/bilingualcontext.conf --check-config
# 按用户配置指定的服务地址拉取模型；第一次可能较慢。
scripts/setup-translator.sh
```

`setup-translator.sh` 通过 HTTP 调用配置地址的 `/api/pull`，不要求本机安装 Ollama CLI。也可以使用 Ollama 自身的工具提前部署模型。

### 2. 前台运行，不依赖 systemd

从源码运行：

```sh
python3 translator/daemon.py
```

从安装产物运行：

```sh
/path/to/prefix/bin/yiban-translator
```

首次启动预热完成后才建立 socket。终端出现 `ready on ...` 表示已监听。可由自己的进程管理器启动；关闭整句翻译时进程会正常退出。

### 3. 安装用户服务

CMake 安装会包含 Python daemon、启动器和配置样例，无需保留源码目录。正常使用 `bin` / `share` 安装布局时，执行：

```sh
# 改为实际 prefix；例如 /usr、$HOME/.local 或自定义 prefix。
scripts/install-translator-service.sh /path/to/prefix
```

也可以直接使用已安装的脚本：

```sh
/path/to/prefix/share/yiban-ime/scripts/install-translator-service.sh /path/to/prefix
```

脚本会：

1. 仅在缺失时创建用户配置，保留已有参数。
2. 检查配置，不下载模型。
3. 备份已有同名用户服务文件。
4. 写入指向已安装启动器的 systemd 单元。
5. 启用并重启用户服务。

使用 `--no-start` 只生成配置与服务文件，不操作 systemd，适合打包验证。该脚本不修改 Rime profile，也不重启桌面 Fcitx。

```sh
systemctl --user status bilingual-ime-translator.service
journalctl --user -u bilingual-ime-translator.service -n 50 --no-pager
```

服务在用户登录会话中启动；不是系统级、全用户共享的输入翻译服务。

## 雾凇词库

雾凇是中文词库，CC-CEDICT 是英文释义词典，两者独立更新。使用雾凇不是开启整句翻译的前提。

现有隔离环境中：

```sh
scripts/update-rime-ice-dictionary.sh
```

其他安装环境可显式指定工具与数据目录：

```sh
YIBAN_FCITX_PREFIX=/usr \
RIME_DEPLOYER=/usr/bin/rime_deployer \
RIME_SHARED_DIR=/usr/share/rime-data \
scripts/update-rime-ice-dictionary.sh
```

发行版提供的 `rime_deployer` 路径可能不同，以实际安装为准。自定义共享库目录可通过 `YIBAN_EXTRA_LIBRARY_PATH` 指定。

脚本将更新用户 Rime 目录中的雾凇词典，并写入 `pinyin_simp.custom.yaml`，将 `pinyin_simp` 的主词典设置为 `rime_ice`。**该 custom 文件会被替换**，有个人配置时请先备份并合并。脚本不清除用户学习数据库。

默认跟踪上游 `main`，不是可复现的固定版本。部署固定版本时：

```sh
RIME_ICE_REF=<实际tag或commit> scripts/update-rime-ice-dictionary.sh
```

实际安装 commit 保存为用户 Rime 目录中的 `rime-ice.version`。升级前备份词典和 custom 文件，部署失败时恢复这些文件后重新部署。

## 迁移到另一台机器

建议迁移源码、配置和必要的个人数据，在目标机器重新构建二进制。

1. 在目标机器安装匹配版本的 Fcitx/Rime 与构建依赖。
2. 获取同一项目 commit，按目标系统 libdir 编译、安装 addon 和可选 Rime 补丁。
3. 复制用户 `bilingualcontext.conf`，检查远程地址、模型和自定义绝对路径。
4. 使用新的服务安装脚本生成目标机器的 systemd 单元，不复制旧的绝对路径单元。
5. 按记录的 commit 部署雾凇词库。
6. 如需迁移个人学习数据，使用 Rime 自身的同步/备份机制；先保留目标端原数据。
7. 运行配置检查、单测，并在目标应用中测试输入与光标移动。

不要迁移：构建目录、`.local-env/sysroot`、旧 `.so`、旧 socket/PID/lock 文件、Python 字节码和运行日志。

单独的 Python 服务载荷可在相同 `bin` / `share` 布局下移动 prefix，启动器会相对定位 daemon。移动后必须重新生成 systemd 单元。C++ addon、Fcitx 与 patched Rime 仍应按新 prefix 和目标 ABI 重新构建。

## 更新与卸载

更新前备份配置；重新构建并运行测试，退出 Fcitx 后安装新版，最后启动 Fcitx。不要直接覆盖仍被进程映射的 `.so`。

关闭或卸载翻译服务：

```sh
systemctl --user disable --now bilingual-ime-translator.service
rm "${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user/bilingual-ime-translator.service"
systemctl --user daemon-reload
```

只停服务会让插件无法得到翻译；推荐同时设置 `Enabled=False` 并重新加载 Fcitx。

CMake 没有自动 `uninstall` 目标。使用系统包管理器安装的产物应由包管理器移除；手工安装的产物可参考构建目录中的 `install_manifest.txt` 逐项核对。不要删除共享的 Fcitx/Rime 库、个人配置或学习数据。

若使用了旧的主用户测试安装器，按它的配套脚本恢复：

```sh
scripts/restore-main-user-test.sh
```

## 测试与排错

### 自动测试

```sh
ctest --test-dir build --output-on-failure
```

测试覆盖词典解析、句子状态、光标编辑与缓存、IPC 帧、配置读取及 Ollama 请求构造。模型请求使用 mock/fake，不据此声称真实翻译质量已达标。

有项目内测试 prefix 时，可以启动完全独立的 Fcitx/D-Bus 会话：

```sh
scripts/test-cursor-translation.sh /path/to/addon-build
YIBAN_TEST_DISABLED=1 scripts/test-cursor-translation.sh /path/to/addon-build
```

该测试不重启日常桌面输入法，验证空格隐藏、返回缓存、删除合并、真实 Rime 选词，以及关闭开关。需要构建产物含 `libbilingualcontext.so`。

词典微基准：

```sh
cmake -S . -B build-core -DBILINGUAL_BUILD_ADDON=OFF -DBILINGUAL_BUILD_BENCHMARKS=ON
cmake --build build-core --parallel
./build-core/dictionary_benchmark build-core/dictionary/base.tsv
```

当前微基准只测单词查询，不代表整页分段释义或端到端模型延迟。

### 常见问题

| 现象 | 检查方法 |
|---|---|
| 找不到 Fcitx5Core | 开发包版本是否 ≥ 5.1.22，`CMAKE_PREFIX_PATH` 是否正确 |
| 编译成功但 Fcitx 找不到插件 | libdir 是否与运行中的 Fcitx 一致；addon 描述文件是否安装 |
| 没有整句翻译 | 检查 `Enabled`、当前 IM 是否为 Rime、服务日志、模型是否存在 |
| 修改配置不生效 | 重新加载 Fcitx 并重启 daemon；排查旧 Environment/drop-in 覆盖 |
| socket 连接失败 | 两端配置是否一致，路径是否过长，目录是否属于当前用户 |
| 返回旧句不能立即显示 | 该句是否已缓存，应用是否提供可靠 surrounding text |
| 英文旁注缺失 | 使用的是否为打过补丁的 Rime，词典路径是否仍有效 |
| 首次翻译较慢 | 模型预热、CPU/GPU 性能、网络与模型驻留时间 |
| Ollama URL 可访问但请求失败 | 必须提供 Ollama `/api/generate`，不能直接使用其他厂商的兼容接口 |

## 项目结构

```text
src/addon/          Fcitx 事件、配置、显示与定时器
src/context/        句子状态、光标文本预测、结果缓存
src/dictionary/     本地只读中英词典
src/translation/    C++ IPC 协议与工作线程客户端
translator/         Python daemon 与模型后端
config/             统一配置样例和 Rime 配置
patches/            固定上游版本的 fcitx5-rime 补丁
data/dictionary/    CC-CEDICT 数据及授权说明
scripts/            服务安装、词库更新、开发环境和测试脚本
tests/              单元与集成测试
benchmarks/         词典查询微基准
docs/               调研、历史架构与发布建议
```

[docs/architecture.md](docs/architecture.md) 保留早期设计规划，其中部分机制尚未实现；当前部署与行为以本文和代码为准。

## 已知限制

- 光标返回和外部编辑依赖应用提供 surrounding text；不同工具包、浏览器和终端的支持不同。
- 翻译缓存为每个 InputContext 最近 32 条精确文本记录，不持久化到磁盘。
- 当前单元超过 4096 字节时，光标模式不翻译该单元。
- 客户端目前串行处理请求；慢请求仍可能延迟后续请求，尚未实现模型推理取消。
- 小模型可能误译专名、否定、长句或术语；英文提示不应代替人工确认。
- 默认输出上限为 256 token，长句可能截断；尚未实现完整质量评测与自动分块。
- 默认日志记录长度和请求标识，不主动记录完整输入；异常日志仍应在分享前检查。
- 密码/敏感标志输入框不翻译。配置远程 Ollama 时，句子文本会发送到该地址。
- 本项目不是 Windows/macOS 原生输入法，也尚未提供 deb/rpm/AUR 等维护中的发行包。

## 许可证与发布

- 项目标识由项目维护者提供，原图位于 [assets/yiban-logo.png](assets/yiban-logo.png)。图片不自动纳入源码或词典许可证。
- CC-CEDICT 由 MDBG 发布，数据采用 **CC BY-SA 4.0**，见 [授权说明](data/dictionary/CC-CEDICT-LICENSE.txt)。转换后的词典应保留署名及相应数据授权。
- 雾凇更新脚本会保留上游 `LICENSE` 与实际 commit；再分发时需一并处理相关授权。
- Fcitx5、fcitx5-rime、librime、Ollama 和模型各有独立许可证，应随实际分发内容核对。
- **本仓库自身代码的许可证尚未确定。正式公开发布前需要补充顶层 LICENSE，不能把词典许可证当作全部源码的许可证。**

首发建议使用 **GitHub 仓库 + Releases**，先发布源码和清楚标注支持环境的预览版，再邀请 Rime/Fcitx 社区、V2EX 或 Linux 用户社区试用。更完整的发布顺序见 [docs/releasing.md](docs/releasing.md)。
