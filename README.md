# Bilingual IME

Linux/Fcitx5 双语中文输入法原型。中文候选由 fcitx5-rime/librime 提供；词级英文提示走本地内存词典；句级翻译将由独立、异步的 translator daemon 提供。

当前完成范围：

- 固定上游版本的源码调研与架构设计；
- 本地只读中英词典核心、comment 合并逻辑、单测和微基准；
- 可应用到 fcitx5-rime `3509646289ec88f5c6c3956b343c305275aa8d3b` 的最小候选 comment patch。
- 项目内 `.local-env` 隔离构建：Fcitx5 5.1.22、librime 1.17.0、
  本词典库和打补丁后的 fcitx5-rime 5.1.14；
- 运行级 smoke test：Rime addon、双语词典和 `pinyin_simp` schema 均可加载。

当前没有实现 context manager、IPC、daemon 或 NMT。

## 构建词典核心

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

构建微基准：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBILINGUAL_BUILD_BENCHMARKS=ON
cmake --build build
./build/dictionary_benchmark data/dictionary/base.tsv
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

详见：

- [源码调研](docs/research.md)
- [推荐架构与开发计划](docs/architecture.md)
