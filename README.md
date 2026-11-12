# 倒计时 Countdown

一个纯 Win32 + C++17 编写的桌面倒计时小工具：无边框深色界面、托盘常驻、支持一语/一文、可插拔的一文显示模块。

- 作者：Bu Xin
- 邮箱：buxin0912@qq.com
- 更新地址：https://github.com/BUXIN-A/Countdown

---

## 功能特性

### 倒计时

- 自定义标题、目标日期与时间
- 循环单位：关闭 / 每年 / 每月 / 每天 / 每小时 / 每分钟 / 每秒，到期后自动顺延到下一次
- 可选择是否把起始日计入天数
- 显示格式：`时:分:秒`、`年月日 时分秒`、`天数 时:分:秒`
- 独立字体与字号（支持加载 `fonts\` 目录下的本地字体文件）
- 倒计时背景色与文字颜色可单独设置
- 窗口尺寸模式：高度自适应（宽度手动）/ 全自适应（宽高随文字）/ 自定义宽高
- 圆角可调：自动胶囊或 0～60 像素直角/圆角，一言字号随窗口高度自动缩放

### 一言

- 来源可选：仅本地 / 仅网络 / 本地和网络
- 本地语句放在 `hitokoto\` 目录，读取该目录下所有 `.txt`（UTF-8，每行一句）；目录为空时自动生成 `default.txt`
- 网络句子可按类型筛选，刷新间隔可配置
- 切换时带「渐隐 → 换句 → 渐显」动画
- 已显示过的句子会被记录并优先跳过，一轮全部看过后自动清空记录
- 可单独设置一言字体与加粗

### 一文

- 独立窗口，位置用九宫格锚点 + 像素偏移定位，可单独设置置顶
- 尺寸模式：固定宽自适应高 / 完全自适应 / 自定义宽高
- 独立字体、字号、背景色与文字颜色
- 刷新节奏由显示模块自己声明（在「模块设置」中调整间隔）

### 一文显示模块（DLL）

一文「选哪一篇」由可插拔模块决定，宿主只负责文章池、去重与渲染。

内置模块：

| 模块 | 说明 |
| --- | --- |
| `normal` | 随机显示：从未看过的文章中随机抽取，刷新间隔在其模块设置中调整 |
| `week` | 星期时段：同一星期范围内固定显示同一篇，范围不能重叠；当天不属于任何范围时隐藏一文窗口（只在跨天时刷新） |

模块放在 `lib\article\` 目录，启动时自动扫描。未找到任何模块时，一文回退为内置随机逻辑。

### 窗口与托盘

- 无边框分层窗口，主倒计时与一文窗口都是深色半透明风格
- 九宫格锚点定位（左上 / 顶部居中 / … / 右下）+ 像素偏移
- 托盘图标：左键打开设置，右键菜单「设置 / 退出」
- 开机自启动开关
- 单实例运行：重复启动会唤起已运行实例的设置窗口

### 检查更新

- 「关于」页点击「获取更新」后直接查询 GitHub Release 最新版本
- 有新版本时展示版本号与更新详情，可选择是否更新
- 附件列表支持直接下载到系统「下载」文件夹，或跳转浏览器手动下载
- 附件下载可切换线路：直连 GitHub 或镜像（默认 `https://gh-proxy.com/`），失败后可一键换线重试
- 下载过程显示百分比进度，可随时取消；失败时自动回退到手动下载
- 下载完成后可点击「安装并重启」自动替换程序并重启（`config.json` 保留，旧文件备份为 `.old` 并在下次启动后清理）
- 查询失败时自动打开项目发布页

### 临时数据管理

- 历史记录（已显示过的一文与一言）按「项数」或「文件大小（MB/GB）」限制
- 达到上限后可选「清空全部」或「删除最早 N 项」
- 设置页提供「立即清理」

---

## 系统要求

- Windows 10 / 11（x64）
- 无需安装 VC++ 运行时：程序静态链接 CRT，单个 exe 即可运行

---

## 下载与运行

从 [Releases](https://github.com/BUXIN-A/Countdown/releases) 下载最新版本的压缩包，解压后直接运行 `Countdown.exe`。

建议保持如下目录结构（缺失的目录会在运行时自动创建）：

```
Countdown.exe
config.json            # 配置文件（首次运行自动生成）
lib\article\           # 一文显示模块
  normal.dll
  week.dll
hitokoto\              # 本地一言语句（*.txt，UTF-8，每行一句）
temp\                  # 运行期临时数据与已显示记录
fonts\                 # 可选：本地字体文件
```

---

## 从源码构建

### 环境

- CMake ≥ 3.15
- MSVC（Visual Studio 2022）或 MinGW-w64

### MSVC

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### MinGW-w64

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

### 产物

```
build\Countdown.exe
build\lib\article\normal.dll
build\lib\article\week.dll
```

版本号唯一来源是 `CMakeLists.txt` 中的 `project(Countdown VERSION x.y.z)`，构建时自动生成 `version.h` 与 `version.rc`（exe 文件属性里的版本信息）。

---

## 配置文件

配置保存在 exe 同目录的 `config.json`，`schemaVersion` 为 `2`，按功能分组：

| 分组 | 内容 |
| --- | --- |
| `basic` | 倒计时目标、主窗口布局、开机自启动、临时数据清理策略 |
| `countdown` | 倒计时显示格式、字体、字号、背景色、文字色、窗口尺寸与圆角 |
| `hitokoto` | 一言来源、句子类型、刷新间隔、字体、加粗 |
| `article` | 一文开关、外观、布局，以及当前使用的显示模块与其配置（含刷新间隔） |

各模块的配置分别保存在 `article.modules.<模块id>` 下，切换模块不会丢失已配置内容。

---

## 开发一文显示模块

模块是一个按约定导出函数的 DLL，ABI 只用 C 结构体 + UTF-16 字符串 + UTF-8 JSON，不跨 DLL 传递 STL 对象。

必须导出的函数（名称固定）：

```c
const ArticleModuleInfo* ArticleModule_GetInfo(void);
const char*              ArticleModule_GetConfigSchema(void);
void*                    ArticleModule_Create(const ArticleModuleHostApi* host);
void                     ArticleModule_Destroy(void* module);
void                     ArticleModule_ApplyConfig(void* module, const char* json);
int                      ArticleModule_Select(void* module, int current_index, int reason);
// 可选：返回自动刷新间隔（秒），0 = 不参与定时刷新
int                      ArticleModule_GetRefreshSeconds(void* module);
```

- 宿主通过 `ArticleModuleHostApi` 提供文章池、已显示记录、临时目录、本地时间与随机数
- 模块通过 `ArticleModule_GetConfigSchema` 返回 schema JSON 声明自己的可配置项，设置界面由宿主按类型自动生成，**新增模块无需修改宿主代码**
- 刷新间隔由模块自己决定：导出 `ArticleModule_GetRefreshSeconds` 的模块按返回的秒数定时刷新，未导出则只在跨天时刷新
- 支持的配置项类型：`bool`、`int`、`choice`、`dayRangeList`

接口定义见 [`src/article_module_api.h`](src/article_module_api.h)，示例实现见：

- [`src/modules/normal_module.cpp`](src/modules/normal_module.cpp)
- [`src/modules/week_module.cpp`](src/modules/week_module.cpp)

---

## 目录结构

```
src/
  main.cpp                     程序入口、单实例、模块扫描
  countdown_window.cpp/.h      主倒计时窗口（无边框、托盘、一言、布局）
  article_window.cpp/.h        一文窗口（过渡动画、尺寸模式）
  settings_window.cpp/.h       设置窗口（四标签 + 滚动内容）
  settings_module_config.cpp/.h 由 DLL schema 驱动的通用模块配置对话框
  update_dialog.cpp/.h         检查更新窗口（版本查询、附件下载、进度）
  article_module_api.h         一文模块 ABI
  article_module_registry.cpp/.h 模块扫描与加载
  modules/                     内置显示模块
  config.cpp/.h                分组配置读写
  history.cpp/.h               历史记录与容量控制
  hitokoto.cpp/.h              一言获取与切换动画
  schedule.cpp/.h              刷新计划表
  tray.cpp/.h                  托盘图标与菜单
  utils.cpp/.h                 DPI、字体、颜色、网络等工具
  json.h                       轻量 JSON 解析/序列化
  version.h.in / version.rc.in 版本号模板（CMake 生成）
CMakeLists.txt                 构建脚本（exe + 模块 DLL）；版本号、仓库地址、下载镜像的唯一配置入口
resource.h / resource.rc       图标、控件 ID、自定义消息
countdown.ico                  应用图标
app.manifest                   DPI 感知与主题清单
```

---

## 更新日志

见 [CHANGELOG.md](CHANGELOG.md)。

---

## 许可

本项目基于 [MIT License](LICENSE) 开源。
