# 验证记录 · 1.0.5

## 当前源码界面调整 · 2026-11-14

本次在本机 Windows 上使用便携 w64devkit 编译并运行，构建无警告，EXE 导入表仅包含 Windows 自带 DLL。验证使用合成演示数据，不连接真实账户。

- 最新 658 项渲染与交互检查通过：涵盖紧凑的 480 × 520 DIP 默认窗口、100% / 125% / 150% DPI、默认页面无滚动条、附加额度溢出滚动、各 Tab 和滚动前后的内容宽度一致、分页、标题栏窗口操作及额度倒计时局部刷新。竖向托盘图标覆盖 16–64 像素、细长轮廓、从底部向上填充、低额度颜色边界、空额度与未知状态。
- 新增检查确认边框没有半透明像素；在 0.1%–99.9% 的 13 组比例下，扣除轮廓后的填充面积与剩余量误差小于 0.1 个百分点。另验证 62.4% 已用直接传入绘制，悬停正确显示 37.6% 剩余及限制项名称。
- 窗口收紧时已通过 13 项 UI 冒烟检查、401 项浅色 / 深色重绘检查；本次竖向托盘调整重跑上述 658 项渲染检查，未重复这两组窗口检查。
- 核对生产绘制代码输出的浅色概览、深色概览、明细与设置截图；仓库截图同步更新。
- 补充 Common Controls v6 清单声明，解决本机 Windows 上悬停提示注册失败；渲染测试链接同一份资源清单。重绘测试在首次截取前等待布局与悬停消息稳定。
- Windows 窗口捕获工具返回 `window capture timed out: timed out waiting on channel`，人工视觉核对使用渲染测试输出的原生窗口截图。

## 历史发行验证环境

1.0.5 发行构建日期：2026-11-14（北京时间）。当时环境：Debian 13 x86-64，MinGW-w64 GCC 14.2.0（POSIX），Wine 10.0 + Xvfb。以下历史记录不代表本次重跑范围；真实登录账户和多显示器等验收边界仍见文末。


## 1.0.5 验证

本版仅调整装饰绘制与版本标识，未更改统计、托盘颜色、自启动或布局行为。549 项既有渲染与交互检查、13 项 UI 冒烟检查通过（Wine 环境）；另核对浅色/深色概览、设置和明细的实际截图。

## 1.0.4 历史验证

本版对剩余额度 20%、19%、10%、9% 的颜色边界，以及正常、空额度、未知状态进行实际像素检查，覆盖 16–64 像素；549 项渲染与交互检查、39 项 Windows 平台检查以及 13 项 UI 冒烟检查全部通过（Wine 环境）。

自启动检查使用 `RegOverridePredefKey` 将测试进程的 HKCU 重定向到临时测试键，同时将配置目录重定向到临时文件夹；检查默认启用、带引号的 EXE 路径及 `--tray` 参数、旧设置迁移、关闭后保存与重载、重复关闭、外部移除后不强制恢复。未向开发环境的真实 Run 键添加启动项，未做真实 Windows 重启登录验收。

## 1.0.3 历史验证

本版 378 项渲染与交互检查、13 项 UI 冒烟、401 项重绘检查全部通过。新增检查包括融合标题栏的客户区范围、标题拖动命中、边缘缩放命中、窗口按钮与拖动区域分离、最大化/还原、双击标题、收起及唤回。白色进度条使用 16 / 20 / 24 / 32 / 48 / 64 像素直接渲染，覆盖从空到满及未知状态。

实际移动、双击、缩放和图标显示在 Wine + Xvfb 桌面核对；真实 Windows 10/11 的多显示器、DWM 圆角/阴影及系统贴靠体验尚未进行硬件验收。主窗口仍使用 Windows 的窗口样式和系统命令；未声称验证 Windows 11 最大化按钮悬停贴靠面板。

## 1.0.2 历史验证

- 365 项渲染与交互检查通过：16 / 20 / 24 / 32 / 48 / 64 像素下，使用率 0 / 25 / 50 / 52 / 75 / 99 / 100 及未知状态，检查电池只有白色、不含彩色底块、轮廓边界、填充随剩余额度递减，以及未知状态不等于额度用尽。
- 检查按钮悬停提示已注册、可访问名称保留、会话分页及前后翻页、常用页面无滚动条、额外额度溢出时出现滚动条、内容恢复后隐藏滚动条。
- 检查窗口不使用 `WS_EX_LAYERED`。磨砂质感来自全不透明渐变和细边框，并未启用桌面模糊或窗口透明效果。
- 大数值缩写、单位进位、倒计时局部更新及同一分钟内 59 次计时器调用不触发重绘的检查继续通过。
- 401 项重绘检查通过：浅色、深色主题下，在缩小后确实需要滚动的概览页，共执行 200 次只重绘父窗口及 200 次滚动，逐次比较顶部按钮实际屏幕像素，未发生内容被父窗口擦除。
- 核心 62 项检查在中国和美国时区分别通过；Windows 平台 17 项、UI 冒烟 13 项继续通过。
- 实际界面检查包含浅/深主题、图标提示、概览/明细/设置/会话分页，以及程序内 100% / 125% / 150% DPI 变化。截图均为演示数据。

重绘与图标检查运行于 Wine 10.0，不代表已在用户的 Windows 显卡或第三方浏览器中复现所有闪烁行为。修复针对本工具的绘制冲突、重复重绘及布局更新。

测试源码为 `tests/ui_repaint_tests.cpp` 与 `tests/render_tests.cpp`。前者先启动演示模式；后者复用生产绘制代码并输出各尺寸电池 PNG。两项均不访问真实账户。

下表为此前版本的验证记录，本版未改变相应数据读取与统计算法。1.0.5 实际重跑的项目列于文首。

## 已通过

| 项目 | 结果 |
| --- | --- |
| C++17 Windows x64 GUI 编译 | 通过，`-Os -static -s`，无编译警告 |
| EXE 导入表 | 只有 Windows 自带 DLL；无 libstdc++ / libgcc / winpthread 外部依赖 |
| 核心数据检查 | 62 项全部通过，在 Asia/Shanghai 和 America/New_York 两个时区分别运行 |
| AddressSanitizer + UndefinedBehaviorSanitizer | 核心 62 项通过，无报告 |
| Windows 平台检查 | Wine 下 17 项通过 |
| UI 冒烟检查 | Wine 下 13 项通过 |
| 文件到界面的端到端检查 | 6 项通过，含重复快照、API Key 状态、复制报告和单实例唤醒 |
| 可视检查 | 概览、明细、设置、浅色、深色、滚动、窗口调整、程序化 150% DPI 变化 |
| ZIP 完整性 | 解压校验和 ZIP CRC 检查通过；含单文件 EXE 和源码 |

### 核心覆盖

- 数值字符串、null、缺失字段、NaN、非法百分比、不完整额度窗口。
- 在线/本地两种额度数据格式、附加模型额度、数字/字符串 Credits。
- 带 UTC 与显式时区偏移的时间戳；缺失时区不静默猜测。
- 输入/输出与缓存/推理子项不重复相加。
- 重复及倒序累计快照去重、分叉会话继承用量处理、损坏记录。
- 文件追加、半行追加、截断、删除、归档重复文件、读取预算耗尽后继续。
- 近 30 天模型统计过滤、超过 2 MiB 的日志行跳过。

### Windows / UI 覆盖

- UTF-8/UTF-16 中文路径转换、SHA-256、JWT 显示字段解码。
- 无凭据、损坏 auth.json、API Key、ChatGPT 文件凭据。
- 传统 Windows direct keyring 目标名及 UTF-16 凭据读取（仅测试专用假凭据）。
- 请求头非法字符在发出网络请求前被拒绝。
- 窗口和设置控件显示、页面切换和滚动、Unicode 报告复制。
- 关闭/最小化到托盘、托盘双击/键盘唤回。
- 原生非演示模式读取测试日志，显示正确的 300 Token（输入 240、其中缓存 120）。
- 第二实例快速退出并唤出第一实例。

## 轻量读取测试

测试生成 500 个 JSONL 文件，含 20,000 条 Token 快照和模拟正文行，总计 34,370,390 字节。Linux 原生核心、同一测试进程中的结果：

```text
files=500
first_bytes=34370390
first_ms=208
unchanged_bytes=0
unchanged_ms=3
tokens=2000000
```

这是当前开发环境中的一次扫描测试，**不是 Windows 启动时延、内存占用或网络查询性能承诺**。增量扫描不会重复读取没有变化的文件内容，但仍需要枚举候选文件并检查元数据。

## 尚未验证 / 实现边界

- 真实账户的在线接口返回、实际网络代理、服务器限流、登录令牌过期与轮换，需要在用户的已登录 Windows 电脑上确认。
- 真实多显示器切换、Windows Explorer 重启、系统睡眠/唤醒、系统通知是否被专注模式抑制，尚未进行硬件实测。代码包含对应恢复或降级处理。
- Windows 传统 direct keyring 之外的新加密 secrets 存储不支持直接解密；无可读凭据时显示本地数据与明确的状态。
- 自启动的注册与设置保存已在隔离的临时注册表及目录中验证；未启用开发环境的真实开机启动。
- 编译目标 x64；未测试 ARM64 模拟与 32 位 Windows。
- 没有测试或声称可获取 ChatGPT API / 第三方 API 的账单费用。
- 无发布者代码签名。

## 复现

Linux 核心：`./test.sh`。额外内存检查：

```sh
g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/core_tests.cpp -o dist/core_tests_sanitized
TZ=Asia/Shanghai dist/core_tests_sanitized
```

Windows 平台测试，用 MinGW-w64 编译：

```sh
g++ -std=c++17 -Os -static -s -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 tests/platform_tests.cpp -o dist/platform_tests.exe -lwinhttp -lshell32 -lbcrypt -ladvapi32
g++ -std=c++17 -Os -static -s tests/ui_smoke.cpp -o dist/ui_smoke.exe -luser32 -lshell32
```

先运行 `CodexPulse.exe --demo`，再运行 `ui_smoke.exe`。端到端测试可用 `make_fixture.py` 创建独立的空测试目录，将当前终端的 `CODEX_HOME` 设为该目录，启动普通模式，再运行 `ui_smoke.exe 20`。不应指向真实凭据目录生成测试数据。

当前版本重绘与图标测试可按以下方式编译（MinGW-w64，工作目录为源码根目录）：

```sh
g++ -std=c++17 -Os -s -static tests/ui_repaint_tests.cpp dist/resources.o -o dist/ui_repaint_tests.exe -luser32 -lgdi32
g++ -std=c++17 -Os -s -static -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 tests/render_tests.cpp dist/resources.o -o dist/render_tests.exe -lgdiplus -lwinhttp -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lbcrypt -ldwmapi -lgdi32 -luser32
```

先执行 `build.bat`（Windows）或 `build.sh`（交叉编译）生成 `dist/resources.o`，然后编译上述测试。启动 `CodexPulse.exe --demo` 后执行 `ui_repaint_tests.exe`。`render_tests.exe` 可独立运行；DPI 消息在测试程序自己的进程内发送，输出 100% / 125% / 150% 以及各页面和深色主题的生产界面截图。图标和截图均为合成演示数据。
