# openFences

[English](README.md) | [Русский](README.ru.md) | **简体中文**

> 一个轻量级的 Windows 桌面图标分组管理工具，Stardock Fences 的开源替代。

![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![lang](https://img.shields.io/badge/language-C%2B%2B17-%23f34b7d)
![license](https://img.shields.io/badge/license-MIT-green)

本仓库是 [weiweigogo/openFrence](https://github.com/weiweigogo/openFrence)（MIT）的 fork。本 fork 关注：英文、俄文、中文三语文档，完善的多语言界面支持（新增俄语），以及长期的 Linux 移植计划（见[路线图](#路线图)）。

---

## 特性

- **桌面图标分组** — 在桌面上创建半透明毛玻璃围栏（Fence），将图标拖入其中自动归组
- **毛玻璃效果** — Acrylic 风格背景：降采样模糊 + 饱和度增强 + 噪点，贴近 Win11 原生观感
- **Win11 风格右键菜单** — 自绘 Fluent Design 菜单，带圆角、毛玻璃背景、软阴影
- **系统托盘常驻** — 托盘图标右键菜单：新建围栏、隐藏/显示全部、切换语言、开机自启、桌面快捷方式
- **配置持久化** — 围栏布局、图标位置、外观设置自动保存至 `%APPDATA%\Open Fences\config.json`
- **多语言** — 支持 English / 中文 / Русский；默认跟随 Windows 语言，新增语言只需添加一个 JSON 文件（见 [docs/LOCALIZATION.md](docs/LOCALIZATION.md)，英文）
- **极轻量** — 单 exe（~780KB），无依赖，无需安装 VC++ 运行库，拷贝即用

---

## 快速开始

> **安全提示**：上游曾把预编译的 `.exe`/`.dll` 提交进仓库，本 fork 已移除。请勿运行来源不明的预编译文件，建议从源码自行构建。当前版本不会向 Explorer 注入 DLL（`experiments/legacy-hook` 目录是早期设计遗留、未被使用的代码）。

1. 从源码构建（见[构建](#构建)）
2. 将 `openfences.exe` 放到任意目录，双击运行
3. 右键右下角托盘图标 → 设置语言、开机自启、创建桌面快捷方式
4. 在桌面上拖拽图标到围栏中即可

> **提示**：如果桌面图标被隐藏导致围栏消失，右键托盘 →「显示桌面图标」即可恢复。围栏会自动停靠在 DefView 上等待图标列表恢复。

---

## 构建

```bash
# 前置条件：Visual Studio 2017+（含 Windows 10 SDK）、CMake 3.20+

git clone <本仓库>
cd openFences
cmake -B build -G "Visual Studio 15 2017" -A x64
cmake --build build --config Release --target openfences
```

产物：`build/Release/openfences.exe`（~780KB，静态链接 CRT）

---

## 项目结构

```
openFences/
├── CMakeLists.txt
├── app/                       # 应用程序
│   ├── include/               # 头文件，目录结构与 src/ 对应
│   ├── src/
│   │   ├── main.cpp           # 入口、所有者窗口（定时器、热键）
│   │   ├── fence/             # 围栏窗口与围栏列表：布局、鼠标、键盘、菜单、排序、映射文件夹
│   │   ├── render/            # Direct2D 渲染、背景滤镜、图标提取、菜单图标
│   │   ├── menu/              # Win11 风格右键菜单、Explorer 外壳菜单
│   │   ├── settings/          # 外观设置面板
│   │   ├── shell/             # 桌面宿主 / DPI、桌面图标列表、Explorer 排序方式
│   │   ├── tray/              # 通知区域图标及菜单
│   │   └── core/              # config.json、本地化、COM 指针辅助、资源 ID
│   └── resources/             # app.ico、lang/*.json（每种界面语言一个文件）
├── tools/                     # 研究性试验与诊断程序（默认不构建）
├── tests/                     # 单元测试（本地化、图标网格、图标排序、图像滤镜、列表滚动条）
├── experiments/legacy-hook/   # 早期设计遗留、未使用的 DLL
├── docs/                      # 设计笔记、LOCALIZATION.md、BUGS.md
├── CLAUDE.md                  # 开发指引（供 AI 辅助）
└── README*.md
```

---

## 配置

配置文件位置：`%APPDATA%\Open Fences\config.json`

```json
{
  "version": 1,
  "language": "zh-CN",
  "hideAll": false,
  "fences": [
    {
      "id": "f1",
      "title": "应用",
      "x": 75, "y": 75,
      "w": 420, "h": 420,
      "collapsed": false,
      "icons": [ ... ],
      "bg": [0.08, 0.08, 0.10, 0.55],
      "titleColor": [0.05, 0.05, 0.07, 0.80],
      "fontSize": 18.0,
      "titleAlign": 0
    }
  ]
}
```

`language`：语言代码（`en`、`ru`、`zh-CN`）。缺省时跟随 Windows 语言。旧配置中的数字（`0` = English，`1` = 中文）会在加载时自动转换。

---

## 技术栈

- **语言**：C++17
- **窗口**：Win32 `WS_CHILD` + `WS_EX_LAYERED` + `UpdateLayeredWindow`
- **渲染**：Direct2D、DirectWrite、WIC
- **JSON**：[nlohmann/json](https://github.com/nlohmann/json)
- **构建**：CMake + MSVC（VS2017+），x64，静态链接 CRT（`/MT`）

---

## 路线图

- [x] 三语文档（英文、俄文、中文）
- [x] 俄语界面与基于键的本地化系统（每种语言一个 JSON 文件，自动检测系统语言）
- [ ] 从版本库中移除已提交的构建产物
- [ ] Linux 移植（上游 issue #8 已提出）。当前代码基于 Win32 / Direct2D，需要新的界面后端；各桌面环境（KDE Plasma、GNOME 等）的桌面图标模型也不相同

---

## 许可证

[MIT](LICENSE)，保留上游作者的版权声明。

## 致谢

- 上游项目：[weiweigogo/openFrence](https://github.com/weiweigogo/openFrence)
- [nlohmann/json](https://github.com/nlohmann/json)
- Stardock Fences 提供了最初的灵感
