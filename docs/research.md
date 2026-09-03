# Fcitx5 / fcitx5-rime / librime 源码调研

> 调研日期：2026-09-02。本文针对当日上游 `master` 的固定提交，不把发行版仓库中的旧包误称为“当前 API”。所有 GitHub 链接均固定到 commit SHA，以免行号随分支漂移。

## 1. 基线版本与方法

| 项目 | 固定提交 | 源码声明版本 | 备注 |
|---|---|---:|---|
| Fcitx5 | [`cdd0b9d`](https://github.com/fcitx/fcitx5/commit/cdd0b9d900770d1ad1229d759213215d5dc23a90) | 5.1.22 | 最新 tag 为 5.1.21；master 已声明 5.1.22 |
| fcitx5-rime | [`3509646`](https://github.com/fcitx/fcitx5-rime/commit/3509646289ec88f5c6c3956b343c305275aa8d3b) | 5.1.14 | 要求 Fcitx5 5.1.22 |
| librime | [`13faefe`](https://github.com/rime/librime/commit/13faefe2819d01fce208752c2539744094bb4787) | 1.17.0 | 当前 release 版本也是 1.17.0 |
| fcitx5-gtk | [`3b18d2a`](https://github.com/fcitx/fcitx5-gtk/commit/3b18d2ab7401d4233daf38bbc5896f1703685a43) | master | 用于核对 GTK surrounding text |
| fcitx5-qt | [`9c9be62`](https://github.com/fcitx/fcitx5-qt/commit/9c9be6229b3ffdabe4e5848ac1e3c346f5144b98) | master | Qt 5/6 共用 platform input context 实现 |

当前工作机没有 `Fcitx5Core.pc` 和 `rime.pc`。因此本文能做源码核对，但在装好匹配的开发包或建立上游构建前，不能声称插件已经编译通过。GitHub 的 Fcitx5 `releases/latest` 仍可能指向较旧 release，不适合作为最新源码判断依据。

## 2. 从按键到中文候选的真实调用链

```text
frontend InputContext::keyEvent
  -> Instance 的 InputMethod phase
  -> InputMethodEngine::keyEvent
  -> RimeEngine::keyEvent
  -> 每个 InputContext 的 RimeState::keyEvent
  -> librime C API process_key(session, keycode, mask)
  -> librime Session::ProcessKey
  -> ConcreteEngine::ProcessKey
  -> processors_（speller/editor/selector 等 schema 配置链）
  -> fcitx5-rime RimeState::updateUI
  -> librime get_context
  -> RimeCandidateList
  -> Fcitx InputPanel
```

具体证据：

- Fcitx 核心在 [`instance.cpp:978-988`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/instance.cpp#L978-L988) 的 `InputMethod` phase 查当前 engine 并调用 `engine->keyEvent(*entry, keyEvent)`。
- fcitx5-rime 的 [`rimeengine.cpp:486-506`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimeengine.cpp#L486-L506) 获取该 `InputContext` 的 `RimeState` 并转交按键。
- [`rimestate.cpp:164-238`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L164-L238) 调用 `api->process_key(session, ...)`，再检查 `get_commit()`，最后更新 UI。
- librime C API 的 [`rime_api_impl.h:164-179`](https://github.com/rime/librime/blob/13faefe2819d01fce208752c2539744094bb4787/src/rime_api_impl.h#L164-L179) 把调用转给 `Session::ProcessKey`；[`service.cc:26-28`](https://github.com/rime/librime/blob/13faefe2819d01fce208752c2539744094bb4787/src/rime/service.cc#L26-L28) 再转给 `Engine`。
- [`engine.cc:99-123`](https://github.com/rime/librime/blob/13faefe2819d01fce208752c2539744094bb4787/src/rime/engine.cc#L99-L123) 依次运行 processors 和 post-processors。因此 librime 并不存在一个固定写死的“拼音类”；实际拼音处理链由 schema 组装，典型组件包括 `speller`、`editor`、`selector` 和 translator/filter。

### Rime session 所有权

`RimeEngine` 用 `FactoryFor<RimeState>` 注册名为 `rimeState` 的 per-input-context property（[`rimeengine.cpp:183-186`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimeengine.cpp#L183-L186)、[`rimeengine.cpp:371-385`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimeengine.cpp#L371-L385)）。`RimeState` 构造时从 `RimeSessionPool` 请求 session（[`rimestate.cpp:45-67`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L45-L67)）。这也说明双语句子状态不能放在一个无区分的全局字符串中：Fcitx 可能同时有多个 InputContext。

## 3. Rime candidate 如何成为 Fcitx candidate

`RimeState::updateUI()` 通过 `get_context()` 取得 `RimeContext`；有 menu 时构造 `RimeCandidateList` 并交给 `InputPanel::setCandidateList()`（[`rimestate.cpp:394-435`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L394-L435)）。

映射发生在 [`rimecandidate.cpp:18-25`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimecandidate.cpp#L18-L25)：

- `RimeCandidate.text` -> `CandidateWord::setText()`
- `RimeCandidate.comment` -> `CandidateWord::setComment()`

当前页候选在 [`rimecandidate.cpp:61-98`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimecandidate.cpp#L61-L98) 转换；全局候选在 [`rimecandidate.cpp:100-137`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimecandidate.cpp#L100-L137) 懒加载，两个路径都必须装饰，否则分页/虚拟键盘会出现不一致。

Fcitx 的 `CandidateWord` 从 5.1.9 起正式区分 `text()` 与 `comment()`，并提供受保护的 `setComment()`（[`candidatelist.h:41-105`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/candidatelist.h#L41-L105)）。英文释义必须进入 comment，不能拼进候选正文；后者可能污染提交文本、Rime 学习和候选操作语义。

## 4. 候选选择与 commit 路径

- `RimeCandidateWord::select()` 在 [`rimecandidate.cpp:27-30`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimecandidate.cpp#L27-L30) 调 `RimeState::selectCandidate()`。
- [`rimestate.cpp:246-266`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L246-L266) 调 librime 的 `select_candidate...`，读取 `get_commit()`，再执行 `inputContext->commitString(commit.text)`。
- 普通按键处理另有 compose fallback 与 Rime commit 分支（[`rimestate.cpp:219-235`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L219-L235)），切换输入法时还可能提交 raw input/composition（[`rimestate.cpp:440-475`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L440-L475)）。

所以不应在每个 Rime 分支插入 context hook。统一、安全的捕获点是独立 addon 监听 `EventType::InputContextCommitString`：

- [`InputContext::commitString()`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputcontext.cpp#L433-L443) 先 UTF-8 清洗、应用 commit filter，再投递 `CommitStringEvent`；事件没被接受才调用 frontend 的 `commitStringImpl`。
- [`CommitStringEvent::text()`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/event.h#L428-L438) 提供最终文本。
- addon 用公开的 [`Instance::watchEvent()`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/instance.h#L209-L229) 在 `PostInputMethod`/`Default` 观察，绝不 `accept()` 或 `filter()`。
- 同时观察 `InputContextCommitStringWithCursor`（[`event.h:446-460`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/event.h#L446-L460)），避免未来特殊提交遗漏。

该回调只能复制短文本、更新内存状态、增加 generation 和重置 timer；不能访问 socket、SQLite 或模型。

## 5. 在哪里附加英文候选描述

### 结论

当前公开 API 下，最小可靠方案是给 fcitx5-rime 打一个很小的 patch，而不是让独立 addon 事后改候选列表。

原因：`RimeCandidateList` 实现 Pageable/Bulk/Actionable/BulkCursor 接口，但没有实现 Fcitx 的 `ModifiableCandidateList`（[`rimecandidate.h:45-52`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimecandidate.h#L45-L52)）。独立 addon 能拿到 `shared_ptr<CandidateList>`，却没有通用 API 安全替换某个既有 `CandidateWord` 的 comment；`setComment()` 也只对派生 candidate 类开放。

切入点应限于 `src/rimecandidate.h/.cpp`：在 `RimeCandidateWord` 与 `RimeGlobalCandidateWord` 构造时，用 candidate 的中文正文查询只读内存词典，然后把结果与原有 Rime comment 合并。词典应在 addon 启动阶段一次性加载，热路径只做无 I/O、无等待的查找。

完全独立 addon 适合 context/IPC/AuxDown，但不适合当前的原地候选装饰。反过来把所有句子翻译代码塞进 fcitx5-rime fork，会扩大故障面和后续 rebase 成本。推荐小 patch + 独立 addon 的混合架构。

## 6. InputPanel 是否适合句级翻译

适合，但要遵守更新时序。

- `InputPanel` 明确提供 `setAuxUp()`、`setAuxDown()`（[`inputpanel.h:57-65`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputpanel.h#L57-L65)），官方注释的典型布局也把 AuxDown 放在候选附近（[`inputpanel.h:29-50`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputpanel.h#L29-L50)）。推荐展示 `EN: ...` 于 AuxDown。
- Classic UI 读取并绘制 aux（[`inputwindow.cpp:365-385`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/ui/classic/inputwindow.cpp#L365-L385)、[`inputwindow.cpp:916-938`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/ui/classic/inputwindow.cpp#L916-L938)）；DBus client-side UI 也转发 AuxUp/AuxDown（[`dbusfrontend.cpp:218-273`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/frontend/dbusfrontend/dbusfrontend.cpp#L218-L273)）。
- `InputPanel::reset()` 会清空 aux、candidate 和 preedit（[`inputpanel.cpp:121-132`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputpanel.cpp#L121-L132)）；Rime 每次正常 UI 更新先 reset（[`rimestate.cpp:394-399`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L394-L399)）。所以 addon 必须在每个 InputContext property 中缓存最新翻译，并在后续 UI 更新时重注入，而非只在响应到达时写一次。

时序核对：Fcitx 核心的 `InputContextUpdateUI` internal handler 在 `ReservedFirst` 只把 `(InputContext, component)` 加入待刷新队列（[`instance.cpp:1231-1244`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/instance.cpp#L1231-L1244)）；真正 dispatch 前，`UserInterfaceManager::flush()` 才发 `InputContextFlushUIEvent` 并读取当前 panel（[`userinterfacemanager.cpp:285-309`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/userinterfacemanager.cpp#L285-L309)）。因此普通 `immediate=false` 更新可在 addon 的 `PostInputMethod` watcher 中仅设置 AuxDown，无需再调用 `updateUserInterface()`。Rime 当前 InputPanel 更新使用默认的 `immediate=false`（[`rimestate.cpp:434`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimestate.cpp#L434)）。`immediate=true` 会在 addon phase 之前 flush，是明确的例外，MVP 不依赖它。

daemon 新结果到达主线程时，addon 设置 AuxDown 后主动调用一次非 immediate 的 `updateUserInterface(InputPanel)`。由该调用触发的 watcher 只能赋值，不能再次要求 UI 更新，否则递归。

## 7. surroundingText 能做什么、不能做什么

Fcitx `InputContext::surroundingText()`/`updateSurroundingText()` 是公开接口（[`inputcontext.h:164-177`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputcontext.h#L164-L177)）。`SurroundingText` 保存 UTF-8 文本，但 cursor/anchor 是 UCS-4 字符位置，并提供 `isValid()`、`invalidate()` 和 `selectedText()`（[`surroundingtext.h:25-75`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/surroundingtext.h#L25-L75)）。

可靠使用条件必须同时满足：

1. `CapabilityFlag::SurroundingText` 存在；
2. snapshot `isValid()`；
3. `cursor == anchor`，即没有选择区；
4. cursor 前的文本与我们 commit history 的预期后缀相容。

不满足就 invalidate context 并隐藏句子翻译。surrounding text 是校验信号，不是总能取得的应用文档真值。

### GTK

GTK3 module 依赖应用响应 `retrieve-surrounding`/调用 `gtk_im_context_set_surrounding`。它把 UTF-8 byte cursor 换成字符位置，password 时不转发（[`fcitximcontext.cpp:1081-1125`](https://github.com/fcitx/fcitx5-gtk/blob/3b18d2ab7401d4233daf38bbc5896f1703685a43/gtk3/fcitximcontext.cpp#L1081-L1125)）。只有 retrieve 成功才声明 surrounding capability（[`fcitximcontext.cpp:1580-1602`](https://github.com/fcitx/fcitx5-gtk/blob/3b18d2ab7401d4233daf38bbc5896f1703685a43/gtk3/fcitximcontext.cpp#L1580-L1602)）。selection anchor 仅对部分 widget（尤其 GtkTextView）能推断，其他控件常退化为 `anchor=cursor`。

GTK 源码明确说明同步请求可能冻结 LibreOffice，故 focus-in 后延迟到 idle（[`fcitximcontext.cpp:866-873`](https://github.com/fcitx/fcitx5-gtk/blob/3b18d2ab7401d4233daf38bbc5896f1703685a43/gtk3/fcitximcontext.cpp#L866-L873)），commit 后也延迟重新请求（[`fcitximcontext.cpp:880-889`](https://github.com/fcitx/fcitx5-gtk/blob/3b18d2ab7401d4233daf38bbc5896f1703685a43/gtk3/fcitximcontext.cpp#L880-L889)）。所以快照允许滞后，commit 后不可立刻要求它精确包含刚提交的字。

### Qt 5/6

Qt module 查询 `ImSurroundingText`、`ImCursorPosition`、`ImAnchorPosition`，只有值有效、非 password/sensitive 且文本少于 4096 个 Qt 字符时才启用并发送 surrounding text（[`qfcitxplatforminputcontext.cpp:386-497`](https://github.com/fcitx/fcitx5-qt/blob/9c9be6229b3ffdabe4e5848ac1e3c346f5144b98/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp#L386-L497)）。环境变量 `FCITX_QT_ENABLE_SURROUNDING_TEXT` 还能整体关闭它（[`qfcitxplatforminputcontext.cpp:662-665`](https://github.com/fcitx/fcitx5-qt/blob/9c9be6229b3ffdabe4e5848ac1e3c346f5144b98/qt5/platforminputcontext/qfcitxplatforminputcontext.cpp#L662-L665)）。Qt6 的 platforminputcontext 源文件是指向 Qt5 实现的 symlink，因此此结论同时适用当前 Qt6 build。

### Wayland 与 X11

Wayland input-method v1/v2 frontend 收到 compositor 提供的 byte offsets 后，先 invalidate，再验证 UTF-8 和边界、换算字符位置后 setText（v1：[`waylandimserver.cpp:265-286`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/frontend/waylandim/waylandimserver.cpp#L265-L286)，v2：[`waylandimserverv2.cpp:299-322`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/frontend/waylandim/waylandimserverv2.cpp#L299-L322)）。deactivate/focus state changes会主动 invalidate。因此是否完整、及时仍取决于应用和 compositor。

X11 本身没有统一的 surrounding-text 文档接口；GTK/Qt 应用通常由相应 IM module 通过 Fcitx DBus frontend 提供数据，XIM-only 应用不应假定可用。Chrome/Electron/VSCode 又会受 Ozone Wayland、X11、GTK/Qt IM module 选路影响。实现必须 capability-driven，不能仅根据 `WAYLAND_DISPLAY` 或 `DISPLAY` 判断。

## 8. 非阻塞地接收翻译结果

Fcitx 提供两条合适的官方路径：

- `Instance::eventLoop()` 可注册 `addIOEvent()` 和 `addTimeEvent()`（[`eventloopinterface.h:142-157`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx-utils/eventloopinterface.h#L142-L157)）。非阻塞 Unix socket 可以直接接入主 loop；回调必须做有界读写和解析，不能等待 daemon。
- `Instance::eventDispatcher()` 是已连接到主 loop 的 dispatcher。`EventDispatcher::schedule()` 明确线程安全（[`eventdispatcher.h:24-81`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx-utils/eventdispatcher.h#L24-L81)）；worker thread 可借此把结果 marshal 回主线程。

MVP 推荐单个非阻塞 socket + Fcitx event loop，而非常驻 client worker thread；daemon 内部自行串行/线程化模型推理。若连接库迫使使用 worker thread，再用 `eventDispatcher().scheduleWithContext(ic->watch(), ...)`。任何异步闭包不得保存裸 `InputContext*`；除 `TrackableObjectReference` 外还须验证 context-local generation、valid flag、focus 和当前输入法。

debounce 使用一个 per-context `EventSourceTime`，150–300 ms 起步；新 commit 只更新 deadline。响应只在 `(request context token, generation)` 等于当前状态时生效。

## 9. addon 生命周期与状态存放

SharedLibrary addon 继承 `AddonInstance`，factory 继承 `AddonFactory::create(AddonManager*)`，并用 `FCITX_ADDON_FACTORY_V2` 导出。官方定义见 [`addonfactory.h`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/addonfactory.h) 和 [`addoninstance.h`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/addoninstance.h)。fcitx5-rime 的最小实例是 [`rimefactory.cpp`](https://github.com/fcitx/fcitx5-rime/blob/3509646289ec88f5c6c3956b343c305275aa8d3b/src/rimefactory.cpp)。

每个 InputContext 的句子、generation、timer、最新翻译应使用 `FactoryFor<T>`/`InputContextProperty`。官方文档明确指出 Fcitx 可能有多个同时 focused context，并给出了注册方式（[`inputcontextproperty.h:21-118`](https://github.com/fcitx/fcitx5/blob/cdd0b9d900770d1ad1229d759213215d5dc23a90/src/lib/fcitx/inputcontextproperty.h#L21-L118)）。property factory 析构前自动 unregister，watchers/timers/socket source 由 RAII 成员持有并在 addon 析构时撤销。

建议独立 addon 监听：

- `InputContextCommitString` / `InputContextCommitStringWithCursor`：追加 context；
- `InputContextSurroundingTextUpdated`：在可验证时校正，否则 invalidate；
- `InputContextKeyEvent`（观察即可）：Backspace/Delete、Left/Right/Up/Down、Home/End、带 Ctrl 的导航或编辑、粘贴等保守失效；
- `InputContextReset`、FocusOut、InputMethodDeactivated/切换：取消 timer、提升 generation、清理或失效；
- `InputContextUpdateUI`：把缓存的最新有效翻译重新写入 AuxDown；
- InputContextDestroyed 由 property 生命周期和 weak watch 兜底，不能让异步响应引用已销毁对象。

## 10. 最小侵入决策

| 方案 | 候选装饰 | commit/context | 故障隔离 | 上游维护成本 | 结论 |
|---|---|---|---|---|---|
| 完全 fork fcitx5-rime | 好 | 可做但耦合 | 较差 | 高 | 不推荐 |
| 完全独立 addon | 当前 API 无法可靠改现有 Rime candidate | 好 | 好 | 低 | 功能不完整 |
| 小型 Rime patch + 独立 addon | 好 | 好 | 好 | 可控 | 推荐 |

Rime patch 只承担同步、纯内存、不会失败的 word hint；句子 context、IPC、daemon 存活与否都不进入 Rime 按键路径。长期可向 fcitx5-rime upstream 提交一个只读 candidate-comment provider 扩展点，以消除 fork。

## 11. 直接回答十个调研问题

1. **拼音经过哪些核心类？** Fcitx `Instance` -> 当前 `InputMethodEngine` -> fcitx5-rime `RimeEngine` -> per-context `RimeState` -> librime C API -> `Session` -> `ConcreteEngine` -> schema processors/translators。
2. **candidate 如何转换？** `RimeState::updateUI()` 获取 `RimeContext`，`RimeCandidateList` 把 Rime text/comment 映射到 Fcitx `CandidateWord` text/comment。
3. **选择后如何 commit？** `RimeCandidateWord::select()` -> `RimeState::selectCandidate()` -> librime select/get_commit -> `InputContext::commitString()` -> commit event -> frontend。
4. **哪里附英文最安全？** `RimeCandidateWord` 和 global variant 构造器内，用内存词典合并 `CandidateWord::comment()`。
5. **哪里捕获 commit？** 独立 addon 的 `InputContextCommitString` 事件，不散落修改 Rime 分支。
6. **InputPanel 合适吗？** 合适，优先 AuxDown；必须缓存并在 Rime reset 后重注入。
7. **surroundingText 限制？** toolkit/app/compositor 可缺失、截断或延迟；密码/敏感输入禁用；GTK selection 支持不齐；Qt 有 4096 阈值和关闭开关；XIM 不可依赖。
8. **如何不阻塞收响应？** nonblocking socket 注册 Fcitx event loop；或 worker + thread-safe EventDispatcher 回主线程。UI 只在主线程改。
9. **最小修改哪些文件？** Rime 侧主要 `rimecandidate.{h,cpp}` 加小型词典库链接；主项目另建 addon/context/IPC，不改 Fcitx core 和 librime。
10. **fork 还是 addon？** 混合：维护可重放的 fcitx5-rime 小 patch，句级功能为独立 addon，daemon 为独立进程。

## 12. 尚需实机验证的假设

- Classic、Kimpanel、GTK client-side UI、Qt/KDE client-side UI 对 comment 和 AuxDown 的实际间距、截断、换行策略。
- Ubuntu 目标版本及其可安装 Fcitx API；当前 master 要求 5.1.22，发行版包可能更旧。
- Chrome/VSCode 在 Ozone Wayland 与 X11 下的 surrounding snapshots 和 mouse cursor change 通知。
- 词典装载时间、hash lookup P50/P95；未 benchmark 前不宣称 `<1 ms`。
- daemon socket 断开、半包、恶意/损坏 length、5 秒慢响应时的输入延迟与内存上限。

这些项目不是推翻架构的前置条件，但都是进入“可用 MVP”前的验收项。
