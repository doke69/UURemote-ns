# 固件 23.0.1 / 大气层（AMS）1.12.1 适配说明

本分支在原作者 v0.1.6 基础上，针对 **Nintendo Switch 系统 23.0.1 + Atmosphère 1.12.x** 完成构建与运行适配，
并同时照顾 **初代 Switch / Switch Lite（HDH-001）** 两种机型。适配版本号：**0.1.9**（v0.1.7 起适配 23.0.1，v0.1.8 修复真机花屏，v0.1.9 新增网络信息面板）。

## 1. 目标环境

| 项目 | 目标值 | 说明 |
| --- | --- | --- |
| 系统固件 | 23.0.1（兼容 23.0.0） | 23.0.0 引入 42 位地址空间 / ShadowStack、fs `IStorageForBatchRead`、sfdnsres c-ares 等变更；23.0.1 仅更新 `sdb` 与系统版本号 |
| 自定义固件 | Atmosphère / AMS 1.12.1 | 1.12.1 修复了 dns.mitm 的 c-ares 解析与 fs.mitm 的 BatchRead 支持，并把自定义 sysmodule 内存恢复到 40 MB；自带 hbl 2.4.5 + hbmenu 3.6.1 |
| 工具链 | devkitA64 r30 / GCC 16.1.0 / libnx 4.12.0-1 | 与上游锁定版本一致；libnx 4.12.0 已包含 23.0.0 的 svc/spl 类型更新，并带有 21.0.0 之后的 TLS ABI 标记（LNY2 rev1），hbmenu 不会提示 ABI 不兼容 |
| 依赖 | switch-sdl2 2.28.5-4、switch-ffmpeg 7.1-5、switch-curl 7.69.1-5 等 | 均取 `scripts/sdk-windows.lock.json` 的锁定版本 |

> 结论：本项目的功能集合（SDL2 显示/音频、socket、libcurl、fsdev/romfs、hid、applet）在 23.0.1 + AMS 1.12.1 上**不需要任何版本门控代码**；适配主要工作是机型/模式识别、显示模式处理和可诊断性。

## 2. 本次适配内容

### 2.1 机型与运行模式识别（`source/main.cpp`）

- 初始化 `set:sys`（`setsysInitialize()`），用 **`setsysGetProductModel()`** 读取真实机型：
  - `SetSysProductModel_Nx` / `Copper`：初代 Switch（Erista）
  - `SetSysProductModel_Iowa`：Switch V2（Mariko）
  - `SetSysProductModel_Hoag`：**Switch Lite**
  - `SetSysProductModel_Aula`：Switch OLED
- 用 `setsysGetFirmwareVersion()` 读取系统版本，并写入连接诊断日志（`switch/UURemote-ns/connection.log`），便于区分两台机器：
  `console-<机型>-<模式>-fw<主>.<次>.<修订>`。
- 每帧读取 `appletGetOperationMode()`（该函数为 libnx 内部缓存值，无 IPC 开销）：
  - `Console`（TV/底座）→ 显示 "TV 模式"；
  - `Handheld`（掌机 / Switch Lite 恒为掌机）→ 显示 "掌机模式"。

### 2.2 底座 / 掌机显示适配

- 窗口增加 `SDL_WINDOW_RESIZABLE` 标志。devkitPro 的 switch SDL2 驱动在 `SDL_WINDOW_RESIZABLE` 下会在插拔底座时自动调用
  `SDL_SetWindowSize()`：TV 模式 1920×1080、掌机模式 1280×720。
- 创建窗口时按当前模式选择初始尺寸（TV 1920×1080 / 掌机 1280×720）；`SDL_RenderSetLogicalSize(renderer, 1280, 720)`
  保持不变，因此全部 UI 坐标、触控坐标映射和画面黑边计算仍然按 1280×720 逻辑尺寸工作，无需改动。
- **Switch Lite 没有 TV 输出**，`appletGetOperationMode()` 始终返回 `Handheld`，因此 Lite 上永远走 720p 掌机路径，
  不会尝试 1080p 窗口。

### 2.3 界面提示

- 首页底部显示一行环境信息：机型 · 模式 · 系统版本（如 `Switch Lite · 掌机 · 系统 23.0.1`），便于确认程序是否运行在目标机器上。
- 远程桌面覆盖层在 TV 模式下追加“触屏不可用”提示（底座模式下触屏输入不可用，需用 Joy-Con / Pro 手柄操作）。

### 2.4 启动检查（保持上游行为）

- 仍然要求 **hbmenu 完整应用模式**（按住 R 启动游戏进入完整内存 hbmenu 后打开本 NRO）。
  相册小程序模式内存不足（本 NRO 运行时镜像约 45.6 MB：24 MB 代码/数据 + 21.7 MB bss），程序会给出明确提示后退出。
- 退出时释放 `set:sys` 服务。

### 2.5 花屏修复（v0.1.8）

真机串流出现花屏时，电脑端画面正常，问题在接收端 H.264 参考帧丢失。v0.1.8 的处理：

- 解码出错、画面停滞 >1.5 s 或每 4 s 周期性发送 RTCP PLI 请求关键帧（`rtc::Track::requestKeyframe()`），
  连接建立后立即请求一次 IDR；
- 解码器标记损坏的帧不覆盖最后一帧好画面（连续 2 s 无干净帧才恢复显示，防止永久冻结）；
- 解码队列上限提高到 16，溢出时丢弃积压、刷新解码器并请求 IDR；
- NVDEC 硬件解码改用单解码线程（软件解码 3 线程）；
- `switch/UURemote-ns/config.ini` 可用 `decoder=auto|software|hardware` 切换解码方式（环境变量 `UU_DECODER` 同理），
  覆盖层底部状态行显示解码方式、分辨率、解码错误与丢帧计数，便于真机定位。
  如果 NVDEC 路径在 23.0.1 上仍异常，可先用 `decoder=software` 排除驱动因素。

## 3. Switch 与 Switch Lite 差异对照

| 项目 | Switch（含 V2/OLED） | Switch Lite |
| --- | --- | --- |
| TV 输出 | 支持，底座模式 1920×1080 | 不支持，恒定掌机 1280×720 |
| `appletGetOperationMode()` | Docked / Handheld 可切换 | 恒为 Handheld |
| 屏幕 | 6.2"/7" 1280×720 | 5.5" 1280×720（UI 逻辑尺寸相同） |
| 触屏 | 掌机/桌面模式可用；TV 模式不可用 | 始终可用 |
| 手柄 | Joy-Con 可拆卸 / Pro 手柄 | 一体化手柄，无 IR 相机 |
| 性能 | 掌机与底座时钟不同 | 掌机时钟；建议保持 720p30 串流 |
| 检测方式 | `setsysGetProductModel()` 返回 Nx/Iowa/Aula | 返回 `Hoag` |

> 同一份 NRO 会按机型/模式自动适配，因此两个安装包内的 NRO 完全一致（SHA256 相同），只有说明文件按机型区分。

## 4. 安装与启动（23.0.1 / AMS 1.12.1）

1. 确认 Atmosphère 为 **1.12.1**（或更高，且支持 23.0.1），`hbmenu` 为 3.6.1、`hbl` 为 2.4.5（AMS 1.12.1 自带）。
   若整合包在 1.12.0 上，请先升级：1.12.0 的 `dns.mitm` 不拦截新的 c-ares 解析命令、`fs.mitm` 不支持 BatchRead，
   会导致联网与文件系统行为异常。
2. 解压对应安装包到 SD 卡根目录，得到 `switch/UURemote-ns/UURemote-ns.nro`。
3. 在 Switch / Switch Lite 上按住 **R** 启动任意游戏进入完整内存 hbmenu，再打开 **UU Remote NS**。
4. 首页应显示 `v0.1.8`，底部显示机型 · 模式 · 系统版本；若未显示，请确认启动的是本目录下的新 NRO。
5. 首次运行会在 `switch/UURemote-ns/` 生成 `client-id` 和 `connection.log`。

## 5. 真机自检建议

- 首页底部机型信息是否正确：Switch 显示 "Switch/Switch V2/Switch OLED"，Switch Lite 显示 "Switch Lite"。
- 掌机 ↔ 底座切换：画面应自动在 720p/1080p 间切换，UI 不拉伸；TV 模式下覆盖层出现“触屏不可用”。
- 断开 / 重连、Home、休眠后电脑端无残留按键（上游已有 release 逻辑）。
- 连接异常时提供 `connection.log`；程序崩溃时额外提供对应 Atmosphère 崩溃报告。

## 6. 已知限制（沿用上游）

- 未实现 H.265、码率 / 分辨率设置、零拷贝显示；NVDEC 路径部分机型/固件上仍可能回退软件解码。
- 音频没有完整抖动缓冲与音画同步；UDP TURN 之外不支持 TURN TCP/TLS，无自动重连。
- 本项目为非官方实验版，需真机联调；本项目不附带 UU TV APK。
