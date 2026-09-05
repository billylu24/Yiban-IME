# 双语中文输入法推荐架构

> 历史设计文档：以下包含尚未实现的规划（如事件循环非阻塞 IPC、daemon 缓存和推理取消）。当前版本使用客户端工作线程，部署、配置与光标行为以 [README](../README.md) 和实现为准。

本文建立在 [`research.md`](research.md) 的固定版本源码证据上。首要不变量是：中文输入路径不等待词典 I/O、socket、SQLite 或模型。

## 1. 架构决策

采用三部分的混合架构：

```text
Linux application
       ^ commitString                         client UI / Classic UI
       |                                               ^
┌──────┴──────────────── Fcitx main process ───────────┴──────────┐
│                                                               │
│  patched fcitx5-rime              bilingual-context addon     │
│  ┌────────────────────┐           ┌────────────────────────┐  │
│  │ librime/RimeState  │--commit-->| per-InputContext state │  │
│  │ RimeCandidateList  │           │ boundary + generation  │  │
│  │ memory dictionary  │           │ debounce + async IPC   │  │
│  │ comment decoration │           │ cached AuxDown         │  │
│  └────────────────────┘           └───────────┬────────────┘  │
└──────────────────────────────────────────────┼───────────────┘
                                               │ nonblocking
                                  length-prefixed JSON / UDS
                                               │
                                    ┌──────────▼───────────┐
                                    │ translator-daemon   │
                                    │ exact cache         │
                                    │ phrase memory later │
                                    │ resident local NMT  │
                                    └──────────────────────┘
```

- **patched fcitx5-rime**：仅在 candidate 构造时做同步的只读内存 hash lookup，并把英文写入 comment。没有 IPC、SQLite、线程或模型。
- **bilingual-context addon**：监听统一 commit 事件，维护每个 InputContext 的句子状态；管理 debounce、非阻塞 IPC、generation 与 AuxDown。
- **translator-daemon**：独立进程，拥有 SQLite/cache/模型。它退出、卡住或返回坏数据时，Fcitx 只隐藏/保留旧提示，中文仍正常。

## 2. 线程与事件模型

```text
Fcitx main/event-loop thread
  key event -> Rime -> candidates -> commit                 [never waits]
                         |
                         +-> append context, generation++   [memory only]
                         +-> reset debounce timer
                                      |
                                 timer fires
                                      +-> enqueue/write nonblocking socket

translator-daemon process
  socket IO -> exact cache -> phrase memory -> resident NMT -> response

Fcitx main/event-loop thread
  readable socket -> bounded frame parse -> validate generation/context
                  -> cache translation -> set AuxDown -> queue UI update
```

MVP client 直接把 nonblocking UDS fd 注册到 `Instance::eventLoop().addIOEvent()`，避免额外 client thread。必须做到：

- socket connect/read/write 都不使用 blocking mode；
- write queue 有界，默认只保留每个 context 最新待发 request；
- 每次 callback 的帧数/字节数有上限，避免饿死按键循环；
- JSON 解析输入最大 64 KiB，字段和 UTF-8 必须验证；
- 失败采用短退避重连，不在 key event 中 connect；
- 所有 UI 与 context property 修改都发生在 Fcitx 主线程。

若后续客户端库无法安全接入 event loop，才增加一个 client IO worker，并只通过 `Instance::eventDispatcher().scheduleWithContext()` 回主线程。模型推理线程始终只存在于 daemon。

## 3. Context 数据模型

```cpp
struct CommittedSegment {
    std::string text;
    uint64_t committedAtUsec;
};

struct ContextState : fcitx::InputContextProperty {
    std::string activeSentence;
    std::string previousSentence;
    std::deque<CommittedSegment> recentSegments;
    uint64_t generation = 0;
    uint64_t lastIssuedRequest = 0;
    bool valid = true;
    std::string latestTranslation;
    uint64_t translatedGeneration = 0;
    // debounce timer / pending request bookkeeping
};
```

状态按 InputContext 隔离。`needCopy()` 保持 false，避免 Fcitx 的 shared-input-state policy 把一个应用的句子复制到另一个应用。

### commit 生命周期

1. 收到 commit event，若当前输入法不是 Rime 或内容为空，按配置忽略。
2. 纯内存扫描 committed segment，按 `。！？!?` 分割。逗号、顿号、分号、冒号保留。
3. 每次有意义的状态变化都 `generation++`，使旧响应失效。
4. 若 commit 包含终止符：终止符前的完整句可成为一次 final translation 请求；随后 `previousSentence` 保存该句，`activeSentence` 接收终止符后的余量。单个 commit 中多个句号也按顺序处理。
5. active sentence 非空且 valid 时重置 200 ms debounce。该值只是 MVP 默认值，必须 benchmark 后调。

用户给出的“句号后立即 clear”与“希望显示完整句最终翻译”存在一个短暂展示需求：实现应保存 `previousSentence` 及其 final generation，允许最终结果短暂显示；新的 active sentence 一旦开始，以新句为准。这样既满足 reset，又不会丢掉句末最有价值的英文。

### 失效和恢复

下列事件取消 timer、清空可显示翻译并 `generation++`：

- 无法用 surrounding snapshot 校验的 Backspace/Delete；
- Left/Right/Up/Down、Home/End、PageUp/PageDown；
- selection、Ctrl+A、未知编辑组合；
- focus out、reset、输入法切换；
- 检测到 mouse/external edit 导致 snapshot 与 commit suffix 不符。

恢复策略分两级：

- 有可靠 surrounding text：从 cursor 前最多一个句边界以来的文本重建 `activeSentence`，设 valid；
- 无可靠 surrounding text：保持 invalid，直到明确句边界后的新 commit 开启一个可信的新句。宁可不显示翻译。

不能在刚 commit 后因 GTK snapshot 尚未异步更新而立刻判错；校验需要容忍“旧 snapshot 是当前 history 的前缀”，等下一次稳定 snapshot 再决定。

## 4. 翻译请求生命周期

状态机：

```text
Idle
  -> commit: Debouncing(generation N)
  -> new commit: Debouncing(N+1), old timer superseded
  -> timer: Queued/Sent(request id R, generation N)
  -> response:
       context gone                      => discard
       invalid/unfocused/wrong IM        => discard
       response generation != current    => discard
       otherwise                         => Displayed(N)
  -> reset/edit/socket failure           => Idle + generation increment
```

generation 是正确性的主判据，request id 用于协议诊断和匹配。响应不能因为“文本相同”而跳过 generation 校验，因为同一文本可能出现在不同应用、焦点或编辑历史中。

## 5. IPC 协议

MVP 使用 Unix Domain Socket 和 4-byte big-endian 长度前缀的 UTF-8 JSON。JSON 的 CPU 成本相对模型推理和 UI 往返可以忽略，开发和诊断价值更高；是否替换二进制格式由 benchmark 决定。

请求：

```json
{"version":1,"id":152,"context":"opaque-token","generation":52,"text":"我觉得这个方法不太好","final":false}
```

响应：

```json
{"version":1,"id":152,"context":"opaque-token","generation":52,"translation":"I don't think this is a very good approach."}
```

协议约束：

- frame 最大 64 KiB；拒绝零长、超长、不完整 UTF-8 和未知必需版本；
- 不把指针、应用文档或用户身份作为 context token；用进程内随机 opaque id；
- daemon 可按连接顺序返回，也允许乱序；client 永远按 generation 判 stale；
- disconnect 清空 partial frame/write queue，但不触碰中文/Rime state；
- socket 放在用户 runtime dir，权限 0600；防止其他本机用户注入翻译结果；
- 日志默认不记录完整用户输入，只记录长度、id、generation、latency 和错误码；debug 明文日志必须显式启用。

## 6. daemon 内部生命周期与故障处理

```text
request
  -> exact in-memory cache
  -> SQLite exact cache (daemon thread only)
  -> phrase/pattern memory (post-MVP)
  -> resident local NMT
  -> write response
```

MVP-5 先用 fake translator 验证 IPC；不把模型选型混进基础正确性。真正 backend 必须常驻加载，并单独 benchmark：cold start、warm P50/P95、RSS、CPU、质量。`opus-mt-zh-en` 只是候选，不能未经比较就定案。

| 故障 | client 行为 | 中文路径 |
|---|---|---|
| socket 不存在 | 标记 offline，退避重连，隐藏 sentence hint | 不变 |
| daemon crash/EOF | 关闭 fd、丢弃 partial response，提升连接 epoch | 不变 |
| inference 5 秒 | 后续 generation 继续增长；晚结果丢弃 | 不变 |
| 响应乱序 | 仅当前 generation 可展示 | 不变 |
| malformed/oversized frame | 断开该连接并记录限流错误 | 不变 |
| SQLite/model failure | daemon 返回 error 或断开；client 不展示 | 不变 |
| addon 内存分配失败等致命异常 | 不在 event callback 外泄异常；禁用 optional subsystem | Rime 应继续 |

## 7. Word dictionary 设计

第一阶段使用小型、审校过的 TSV/JSON 源文件，在启动时编译/加载为只读 `unordered_map<string, WordTranslation>`。候选热路径：

```text
candidate UTF-8 text -> hash lookup -> format max N bytes -> merge comment
```

约束：

- 不在 candidate 构造时打开文件、访问 SQLite、加可能等待的全局锁；
- 最多显示 primary + 一个短 alternative，例如 `method / way`；超长只显示 primary；
- 保留 Rime 原 comment，使用明确 separator；
- 没有命中即零行为差异；
- 数据源许可证与 attribution 单独记录，不能直接把 ECDICT/CC-CEDICT 混合后忽略再分发条款；
- benchmark 分离 lookup 与 candidate-page decoration，总页 P50/P95 才能说明是否无感。

## 8. 源码切入点与文件边界

### 上游 fcitx5-rime 小 patch

| 文件 | 修改 |
|---|---|
| `src/rimecandidate.h` | candidate 构造器接收只读 `WordHintProvider` |
| `src/rimecandidate.cpp` | 当前页及 global candidate 的 comment 合并 |
| `src/rimestate.cpp` | 构造 `RimeCandidateList` 时传 provider（若 provider 不由 engine accessor 提供） |
| `src/rimeengine.h/.cpp` | 持有启动时加载的 provider/config；不得持有 IPC/NMT |
| `src/CMakeLists.txt` | 链接独立 word dictionary 小库 |

建议将改动保存为可对固定 upstream SHA 重放的 patch，而不是复制整个仓库。若实现阶段发现只改 `rimecandidate.*` 并用 engine accessor 已足够，就不改 `rimestate.cpp`。

### 本仓库新增

```text
CMakeLists.txt
cmake/
src/
  addon/
    bilingualaddon.h
    bilingualaddon.cpp
    bilingualfactory.cpp
  context/
    contextstate.h
    contextmanager.h
    contextmanager.cpp
  dictionary/
    bilingualdictionary.h
    bilingualdictionary.cpp
  translation/
    protocol.h
    protocol.cpp
    translationclient.h
    translationclient.cpp
translator/
  main.cpp
  server.h
  server.cpp
  cache.h/.cpp                 # MVP-6 前可为空接口
  nmt.h/.cpp                   # MVP-6 才加入
data/
  bilingual-addon.conf.in
  dictionary/base.tsv
patches/
  fcitx5-rime-word-hints.patch
tests/
  context_test.cpp
  generation_test.cpp
  protocol_test.cpp
  dictionary_test.cpp
benchmarks/
  dictionary_benchmark.cpp
docs/
  research.md
  architecture.md
```

不要新增浮窗、X11/Wayland protocol 或键盘驱动代码。

## 9. MVP 顺序与验收门槛

1. **MVP-0：可复现基线。** 固定三项目 SHA；在 Ubuntu target/container 安装或构建 Fcitx5 5.1.22、fcitx5-rime 5.1.14、librime 1.17.0；未改版 Rime 能安装、输入、卸载回退。
2. **MVP-1：word dictionary library。** 解析、重复项、UTF-8、长度限制单测；启动后 lookup 无 I/O；有 benchmark 基线。
3. **MVP-2：候选 comment patch。** 只接词典；当前页和 global/paging 都显示；candidate select 提交文本与未修改版完全一致；词典缺失时 Rime 正常。
4. **MVP-3：context-only addon。** 统一 commit event 构句并仅在显式 debug 模式输出；边界、多个句号、InputContext 隔离、invalidate 单测。
5. **MVP-4：fake daemon + IPC。** length-prefix、半包、多包、断连、5 秒延迟和乱序测试；kill daemon 时持续输入无卡顿。
6. **MVP-5：AuxDown。** generation 检查、Rime reset 后重注入；Classic/GTK/Qt/KDE、X11/Wayland smoke tests。
7. **MVP-6：真实 local NMT。** 先用固定测试集比较模型/运行时，再选 backend；不要反向污染前五步。

每一步都要求当前改动可编译、单测通过、中文输入 smoke test 通过后再进入下一步。

## 10. 第一阶段具体开发计划

第一阶段只到候选词典装饰，不启动 context/daemon：

1. 建立明确的 Ubuntu 支持矩阵。建议首个开发目标为 Ubuntu 24.04 环境中自带/side-by-side 构建当前 Fcitx stack；系统默认包若 API 太旧，不静默降级。
2. 建立 CMake 工程和 `WordTranslation`/`BilingualDictionary`，先内置十余条测试数据。
3. 写 dictionary unit tests：命中、未命中、primary/alternative、重复 key、坏 UTF-8、过长释义、数据文件缺失。
4. 写 microbenchmark，记录单 lookup 和 5/10 个候选整页装饰的 P50/P95，而不是凭感觉宣布 `<1 ms`。
5. 在临时 fcitx5-rime checkout 上做最小 patch，覆盖 current-page 与 global candidate 两条构造路径；不触碰 commit、session 或 librime。
6. 构建并安装到隔离 prefix，保留原插件；用 `FCITX_ADDON_DIRS`/测试 profile 启动单独 Fcitx 实例。
7. 验收：`英伟达`、`方法` 等显示 comment；没有词典项的候选不变化；选择后提交纯中文；翻页、数字键、鼠标选择正常；删除/损坏词典后中文输入正常。
8. 将 patch、构建命令、测试结果和 benchmark 原始数据纳入仓库，之后才开始 context manager。

## 11. 主要风险

1. **发行版 ABI/API 差异。** Candidate comment 需要 Fcitx >=5.1.9，而当前 fcitx5-rime master 要 5.1.22；Ubuntu 默认包不能假定满足。
2. **UI 实现差异。** comment/AuxDown 虽有核心 API，但不同 UI 的宽度、换行和 client-side rendering 不一致。
3. **独立 addon 的候选权限不足。** 已通过源码确认，不能以“理论上可 decorator”代替实际 API；需要小 patch。
4. **surrounding text 不可靠。** 特别是 Electron/XIM、GTK 延迟和 mouse edits；必须保守失效。
5. **隐私。** 即使离线，句子也可能进入日志/SQLite；默认日志不能含明文，cache 需要可关闭和清理策略。
6. **主 loop 公平性。** nonblocking 不等于免费；大 JSON、大量帧或无限 write queue 仍会卡按键，故必须有界。
7. **生命周期竞态。** InputContext 可在响应途中销毁/失焦；weak watch + context token + generation 三重验证。
8. **词典许可证与质量。** ECDICT、CC-CEDICT、自定义词表的许可、专名大小写和短语自然度要单独治理。
9. **最终句显示语义。** 句号会清 active context，但 final response 仍需短暂显示；`previousSentence` 明确承担这个状态，避免互相矛盾。
10. **模型自然度与实时性冲突。** 不在 MVP 假定某模型最好；必须用给定口语测试集和目标 CPU 实测。

## 12. 性能与可靠性验收

至少记录：

- baseline Rime key-to-candidate latency 与打 patch 后差值；
- word lookup/page decoration 的 P50/P95/P99；
- fake daemon 的 debounce 请求数、IPC round-trip、乱序丢弃数；
- daemon 不存在、被 kill、5 秒延迟、持续输出坏帧时的 key latency；
- 真模型 cold start、warm P50/P95、RSS、CPU；
- 每个平台组合下 surrounding capability、snapshot 长度和更新时机。

硬性通过条件是：所有翻译组件关闭、缺失或崩溃时，Rime 的按键、候选、选择和 commit 与基线一致。
