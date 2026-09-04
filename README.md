# Bilingual IME

Linux/Fcitx5 双语中文输入法原型。中文候选由 fcitx5-rime/librime 提供；词级英文提示走本地内存词典；句级翻译由独立、异步的 translator daemon 提供。

当前完成范围：

- 固定上游版本的源码调研与架构设计；
- 基于 CC-CEDICT 的本地只读中英词典、候选逐条翻译、comment 合并逻辑、单测和微基准；
- 可应用到 fcitx5-rime `3509646289ec88f5c6c3956b343c305275aa8d3b` 的最小候选 comment patch。
- 项目内 `.local-env` 隔离构建：Fcitx5 5.1.22、librime 1.17.0、
  本词典库和打补丁后的 fcitx5-rime 5.1.14；
- 运行级 smoke test：Rime addon、双语词典和 `pinyin_simp` schema 均可加载。
- 按 InputContext 隔离的整句状态机、200ms 防抖、可配置断句符和过期结果丢弃；
- 独立 CTranslate2 translator daemon，使用本地 OPUS-MT 中英神经翻译模型；
- 整句英文通过 `AuxDown` 显示，不改变候选词或中文提交内容。

候选词翻译已覆盖 CC-CEDICT 收录的简繁体词条；未收录的复合词会尝试按最长词条
分段翻译。整句翻译不复用这些释义，而是由神经翻译模型生成自然英文。

## 构建词典核心

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 整句翻译

安装本地翻译运行时和约 83 MiB 的中英模型：

```sh
scripts/setup-translator.sh
```

使用本项目的 Fcitx prefix 构建并安装 addon：

```sh
cmake -S . -B .local-env/build/bilingual \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/.local-env/prefix" \
  -DCMAKE_PREFIX_PATH="$PWD/.local-env/prefix;$PWD/.local-env/sysroot/usr"
cmake --build .local-env/build/bilingual
cmake --install .local-env/build/bilingual
```

主用户测试安装器会启用 `bilingual-ime-translator.service` 用户服务，Fcitx 启动器也会
确认服务已启动。中文提交后停顿约 200ms，候选框下方会显示 `EN: ...`；
按空格或输入 `。！？.!?` 时立即请求最终整句翻译。Backspace、Delete 和导航键不会
清空插件维护的句子缓冲。

断句符可在 Fcitx 配置工具的 `Bilingual sentence translation` addon 页面修改，
也可直接编辑 `~/.config/fcitx5/conf/bilingualcontext.conf`。每项只能放一个 UTF-8
字符；空格使用可见的特殊值 `space`。默认配置等价于：

```ini
[SentenceBoundaries]
0=space
1=.
2=!
3=?
4=。
5=！
6=？
```

构建微基准：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBILINGUAL_BUILD_BENCHMARKS=ON
cmake --build build
./build/dictionary_benchmark build/dictionary/base.tsv
```

## fcitx5-rime 集成

先把本库安装到隔离 prefix，再对固定的 fcitx5-rime checkout 应用 patch：

```sh
cmake --install build --prefix /path/to/prefix
git -C /path/to/fcitx5-rime apply \
  /path/to/Fcitx5-Giaok/patches/fcitx5-rime-word-hints.patch
```

配置 fcitx5-rime 时把 `/path/to/prefix` 加到 `CMAKE_PREFIX_PATH`。找不到本库时 patch 使用 `QUIET` 并编译成原始 Rime 行为；运行时词典缺失或格式错误时也只关闭 word hints，不阻止 Rime 启动。

## 隔离环境

完整工具链位于 `.local-env`，没有安装或覆盖系统 Fcitx。进入环境：

```sh
source scripts/activate-local-env.sh
```

复测已安装产物：

```sh
scripts/smoke-test-local-env.sh
```

smoke test 使用 `.local-env/xdg-*` 下的独立配置与用户数据，不会读取或写入
用户日常使用的 `~/.config/fcitx5` 和 `~/.local/share/fcitx5`。它只验证无显示服务
场景下的进程启动、addon 加载、词典加载和 Rime schema 部署；候选框的视觉检查仍需
在真实 Wayland/X11 会话中进行。

## 临时切换主用户环境

将当前桌面会话临时切换到项目内 Fcitx5 5.1.22 和双语 Rime：

```sh
scripts/install-main-user-test.sh
```

安装器会先备份当前 profile 和 autostart 文件，再把 `rime` 设为默认输入法；原来的
`pinyin` 仍保留在输入法列表中。该测试不覆盖 `/usr`。恢复 Ubuntu 自带的 Fcitx5
和原始配置：

```sh
scripts/restore-main-user-test.sh
```

备份保留在 `~/.local/state/giaok-keyboard/backups/`。回退时，本次测试产生的
Rime 用户数据也会移入对应备份目录，不会直接删除。

详见：

- [源码调研](docs/research.md)
- [推荐架构与开发计划](docs/architecture.md)
