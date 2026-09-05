<p align="center">
  <img src="assets/yiban-logo.png" alt="Yiban IME 标识" width="180" />
</p>

<h1 align="center">Yiban IME · 译伴输入法</h1>

<p align="center"><a href="README.md">English</a> | <strong>简体中文</strong></p>

**输入中文的同时，学习英文。**

译伴是一款基于 **Fcitx5 + Rime** 的 Linux 输入法，帮助英语学习者在日常中文输入中，了解对应的英文词义和句子表达。

中文候选旁显示本地英文释义；可选的整句翻译显示在候选框下方，并跟随光标所在的句子更新。提交给应用的仍是你选中的中文。

## 功能

- **候选词英文释义**：本地查询 CC-CEDICT，无需模型或网络。
- **整句翻译**：支持本机或远程 Ollama，可单独开启或关闭。
- **跟随光标**：空格结束当前单元后隐藏提示；返回旧句恢复缓存，修改文字后更新翻译。
- **熟悉的中文输入**：Rime 负责拼音输入与用户学习，可使用雾凇拼音词库。

输入法列表显示为 **Yiban**，内部保留 `rime` 标识，兼容原有配置与学习数据。项目提供的输入法列表仅包含 **英文键盘（美国）** 和 **Yiban 中文输入法**。

## 安装

需要 Linux、支持 C++20 的编译器、CMake ≥ 3.20、Python ≥ 3.10、Fcitx5 Core/Config ≥ 5.1.22 开发包，以及 nlohmann-json ≥ 3.11。

```sh
git clone https://github.com/billylu24/Yiban-IME.git
cd Yiban-IME

# 安装目录与库目录需要匹配当前运行的 Fcitx。
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

后续安装、Rime 候选释义补丁及翻译服务设置见[部署指南](docs/deployment.zh-CN.md)。使用私有安装目录时，还需要执行用户图标安装步骤，让桌面托盘能够找到图片。

## 配置

默认配置文件为 `~/.config/fcitx5/conf/bilingualcontext.conf`，支持 XDG 配置路径。参见[完整示例](config/bilingualcontext.conf)。

```ini
Enabled=True
OllamaUrl=http://127.0.0.1:11434
OllamaModel=qwen3.5:0.8b
DebounceMs=200
```

设置 `Enabled=False`，即可关闭整句翻译，保留中文输入与候选英文释义。修改配置后：

```sh
systemctl --user restart bilingual-ime-translator.service
fcitx5-remote -r
```

模型需要预先安装在指定的 Ollama 服务器上。当前使用 Ollama `/api/generate`，不支持直接填写 OpenAI-compatible 接口。使用远程服务器时，待翻译的句子会发送到该地址。

## 使用说明

- 翻译是学习辅助，模型可能误译专名、术语或长句，重要表达请核实。
- 光标返回后恢复翻译，依赖应用提供附近文本；不同应用支持程度不同。
- 已在 Linux x86_64 环境测试，其他发行版、桌面和应用的兼容性仍需验证。

[详细安装、迁移、测试与排错 →](docs/deployment.zh-CN.md)

## 致谢与许可

基于 Fcitx5、Rime、CC-CEDICT、可选的雾凇词库和 Ollama。CC-CEDICT 数据使用 [CC BY-SA 4.0](data/dictionary/CC-CEDICT-LICENSE.txt)，其他组件保留各自许可证。项目标识由维护者提供。本仓库自身源码的许可证尚未确定。
