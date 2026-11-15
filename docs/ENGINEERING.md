# MemSweep 工程手册

## 1. 架构

```
src/common.h     基础类型、Config、日志、字符串/格式化工具
src/core.h/.cpp  NT 快照、指标（压缩/池/提交）、策略、清理引擎、监控线程、自检
src/ui.h/.cpp    GDI+ 自绘 UI、托盘、热键、主题、主屏 + 一层 Sheet
src/main.cpp     入口、CLI、单实例转发、控制台输出
res/             图标、UAC 清单、版本信息
```

依赖方向：`ui → core → common`；`main → ui/core`。`core` 不依赖 GDI/窗口。

## 2. 不可破坏的约束（Invariants）

1. **永不调用** `MemoryFlushModifiedList`（脏页刷盘）、注册表刷盘、全局 `MemoryEmptyWorkingSets`。
2. **前台/焦点保护**：焦点窗口、父链、120s 焦点历史、系统关键进程、白名单绝不被修剪。
3. 自动模式：常规压力「空闲后台修剪 + 低优先级缓存」可同次执行；只有真实枯竭
   （可用 < 512MB 或使用率 ≥ 95%）才升级为「非空闲后台修剪 + 全量缓存」，且全量缓存
   还要求空闲 < 128MB 或可用 < 512MB，避免高压下每 3 分钟反复清空。硬缺页尖峰只门控
   常规缓存清理；真实枯竭可越过该门控。自动模式永不合并页面。
3a. **离开时维护**（默认关）：空闲 ≥ idleDeepSecs 且（负载 ≥ 阈值或提交 ≥ 85%）才触发；
   只做空闲后台修剪 + 低优先级缓存 + 页面合并，**永不清全量缓存/文件缓存**；
   用户输入即经 EventHandle 中止；失败/中止也遵守 idleDeepCooldownSec 冷却。
4. 单文件、零运行时依赖、无网络、无遥测。
5. 每次清理后 `EmptyWorkingSet(self)`；隐藏/最小化立即释放双缓冲；
   `SeDebug/SeProfile/SeIncreaseQuota` 只在内核操作前开启、操作后关闭，UI 启动不常驻特权。
6. 深度整理（合并页面）只可挂在**手动清理（含热键）**上，且默认关闭；自动模式永不合并。
   因为实测可能 7–52 秒，必须在 Advanced 中显式开启。
7. 本工具禁用 IME/TSF 注入（`ImmAssociateContextEx`），主窗口与全部 Edit 均需处理；
   编辑框因此只支持 ASCII。
8. **UI 线程禁止同步** `PowerShell` / WMI / `schtasks` / 可能 >50ms 的 I/O；
   一律工作线程执行，`PostMessage` 回主线程回填。系统页的浅层指标刷新才允许在 UI 线程。
9. **效果统计**必须区分「空闲增量」与「可用增量」：清理气泡/上次清理优先显示空闲增益，
   不得再把可用内存的 0 变化描述成“释放 0MB”；无净增益的中止不得覆盖“上次清理”。
10. **并发纪律**：后台线程一律读 `GetConfigSnapshot()` 的不可变副本；保护名单写走
    `RebuildWhitelist` 锁内 swap；跨线程计数（hf/lastGain/pause/idleSecs/g_zh/gameMode）
    必须用 `std::atomic`；快照缓冲必须 `thread_local`。
11. **全屏/演示模式**（D3D 全屏 + 演示）暂停自动清理；不用 `QUNS_BUSY`（NVIDIA Overlay 误报）。
12. 页面合并的 `Get-MMAgent` 查询失败时必须显示“需要管理员权限/未知”，不得把空输出
    当作功能已关闭；页面合并必须带 `EventHandle` + 超时看门狗，不允许不可中止的扫描。
13. **文案口径**：UI 只用任务管理器术语（使用中/可用/已缓存/已提交/内核内存/专用工作集）；
    通知以“已释放约 X”开头（空闲/可用净增取大者），单指标；标签与按钮不带句号；
    同一概念全应用只用一个词；行话（工作集/抖动/EcoQoS 字样）不得出现在用户可见文案。
    阈值 = “内存高于”；列表 = “内存占用前 N”；估算 = “可释放约”；合并 = “合并中”。
    开关副文案必须写清「状态 + 效果」（“已开启 · 不弹完成通知”），不得反向描述；
    不得使用绝对承诺（“零卡顿”“无感”）。
14. **设置减法**：高级设置固定 5 项；抖动保护内置护栏不暴露开关；
    页面合并跟随「清理力度=深度」，不设独立开关；静默模式在设置页（不到高级层）；
    新增设置项必须先证明不能并入现有项；主题/语言另有标题栏快切。
15. **布局完整性 / 模态绘制**：`BuildLayout` 必须为当前屏幕的**所有**绘制几何赋值，
    不得按 Sheet 状态跳过首页矩形（`Layout()` 的 `RectF` 默认构造不初始化，读到即随机坐标，
    v1.9.4 左上角残影教训）。Sheet 打开时 `DrawAll` 只画「背景 + 遮罩 + Sheet」，
    不画首页/标题栏——16px 边缘只会露出被裁切的碎片，视觉上更像故障。

## 3. 构建与测试

```cmd
build.cmd          :: release（requireAdministrator）
build.cmd dev      :: dev（asInvoker，可免提权 UI/清理测试）
build.cmd test     :: dev + /selftest + /idlecheck + /uipng + /checkpng
```

工具链：`..\tools\zig-x86_64-windows-0.17.1\zig.exe`（zig c++，mingw-w64 静态链接）。
告警集：`-Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith`（要求零警告）。

## 4. 诊断命令

| 命令 | 作用 |
|---|---|
| `/selftest` | 策略引擎单元测试（exit 0/1） |
| `/dryrun` | 只列出将修剪的进程并写日志，不动作 |
| `/idlecheck` | 输出 Top30 进程的 WS/空闲秒数/保护/修剪记忆 |
| `/uipng` / `/checkpng` | 离屏渲染主屏/系统 Sheet（520×600 基准，可配 `MEMSweep_DPISCALE`/`MEMSweep_THEME`） |
| `/clean` | 单次手动清理（需管理员；已运行实例则转发） |
| `/combine` | 深度整理：合并重复物理页（10–60s，需管理员；已运行实例则转发） |
| `/trimpid <PID>` | 精准修剪单个进程工作集 |
| `/pause` / `/resume` | 暂停/恢复自动守护（已运行实例则转发） |
| `/show` / `/exit` | 前置面板 / 退出实例 |

日志：`%APPDATA%\MemSweep\log.txt`（256KB 轮转）；配置：`%APPDATA%\MemSweep\config.ini`。

## 5. 内存预算（本机 100% DPI；150% 时双缓冲约 +1.6MB）

| 状态 | Private | Working Set |
|---|---|---|
| 托盘隐藏 | **6.5MB** | 11.1MB（隐藏即释放双缓冲 + HeapCompact） |
| 全新托盘启动 | **5.9MB** | 5.2MB |
| 主屏 | **8.2MB** | 12.7MB（WS 随自修剪波动） |
| 设置/系统 Sheet | **约 9.7–9.9MB** | 约 10–15MB（含窗口双缓冲） |
| 未禁用 IME（对照） | 71.8MB | 90MB（Chatterfly 注入） |

说明：系统 DLL（SHELL32/DWM/GDI+ 等）按需映射；`Private` 含 GDI+ 堆与字体缓存。
隐藏/最小化会立即 `ReleaseBackBuffer()`，不再等 20 秒；重绘时按需重建。
不得以“关闭 IME”为代价影响其他程序——只对 MemSweep 自己的窗口生效。

## 5.0 Sheet 模型

- `Sheet::None / Settings / Advanced / Processes / System`；没有顶层 Page 枚举、没有顶部导航。
- 主屏 4 组：状态句 + 细环 + 主按钮 + 两个文字入口；进程/系统按需打开。
- Sheet 是主屏上的一层覆盖：scrim + 200ms ease-out 位移；没有页面交叉淡入、没有页面位图。
- 设置 Sheet 单列、无 tabs、无保存按钮；修改即时 `CommitSettings()`（debounce/失焦/关闭时）。
- 高级设置是紧凑 Sheet；进程/系统 Sheet 按内容缩短高度，系统更多状态再展开。
- 原生 Edit 只有白名单一个；Sheet 动画期间隐藏，settled 后显示；切页/关闭时提交。
- 命中：Sheet 打开时只注册 Sheet 控件 + 四周 scrim；主屏/窗口按钮不可点。
- 性能：Sheet 高度 340–568 DIP；无页面过渡缓冲；空闲只跑 1s 指标刷新。
- **动效纪律**：动画步进只在 33ms 定时器（`StepAnimations`，帧率无关指数平滑）；
  环境动效固定 6.7fps、仅 可见+主屏+非清理，隐藏 0 开销；尊重 SPI_GETCLIENTAREAANIMATION；
  实测预算：可见 1.6%（单核）。禁止把动画推进写进数据刷新路径。
- **重绘纪律**：`WM_PAINT` 按 `BeginPaint` 更新区做部分重绘（背景缓冲保留上一帧，
  `DrawAll(clip)` 仅栅格化失效区）；环境动效只失效仪表矩形；禁止全屏 Invalidate 的高频路径。
- **托盘纪律**：图标只在整数百分比变化时重建（DIB+GDI+ 绘制开销大）；
  tooltip 变化走 `NIF_TIP` 轻量更新；`Shell_NotifyIcon` IPC 不进入每秒路径。
- **体积纪律**：`-Oz + gc-sections + strip` 为该工具链实测最优；已否决
  `-falign-*`/`-fmerge-all-constants`（零收益）与 unwind 表移除（CRT `.seh_` 编译失败）。
- **标题栏控件清单**：新增标题栏按钮必须同步四处——`Layout` 矩形、`BuildLayout` 命中、
  `DrawChrome` 绘制、**`WM_NCHITTEST` 的 `HTCLIENT` 排除清单**（漏掉会被 `HTCAPTION`
  拖拽吞掉点击，v1.9.3 教训）。
- **标题栏图标规范**：统一 1.5px 圆头描边、16px 光学尺寸、30×30 悬停面；idle `T.text2`、
  hover `T.text`。主题/语言都显示“点击后”的状态（浅色→月亮、深色→太阳；中文→EN、英文→中），
  两个快切按钮不得一个显示当前、一个显示目标。只有窗口关闭按钮使用 danger 悬停色。

## 5.1 线程模型

- UI 线程：窗口消息、GDI+ 绘制、配置编辑；只做轻量读取。
- `MonitorThread`：等待 `monEvent + LowMemoryResourceNotification`（5s 超时，低内存事件 2s 节流）+ 采样与策略；
  每 tick 计算 `ComputeReclaimable`（估算主屏“可释放约 X”）；配置一律取 `GetConfigSnapshot()` 不可变副本；
  通过临界区发布 `Metrics`，UI 只做取整比较。
- `SysInfoThread`：`Get-MMAgent` / `Win32_PageFileUsage`，WSL 用 `LxssManager` 注册表键快速探测；
  在本地 `SysState` 上完成查询，`SRWLOCK` 一次性换入；完成后 `WM_APP_SYSINFO`（仅系统 Sheet 可见时刷新）。
- `SysActionThread`：系统修改 + 确认框 + 刷新；启动时复制 `SysState` 快照，`WM_APP_SYSINFO`。
- `AutostartThread`：`schtasks` 创建/删除；`WM_APP_AUTOSTART`。
- `IdleDeepThread`（离开时维护）：空闲判定通过后取 `PlanIdle` 快照执行；
  独立 watcher 线程监视用户输入，`SetEvent(cancel)` → RunClean 修剪循环与页面合并同步中止。
- 任何后台线程不得直接触碰窗口句柄之外的 UI 状态；`HWND` 仅用于 `PostMessage`。

## 6. 代码风格

- 4 空格缩进、K&R 括号、120 列；UTF-8（无 BOM）；`snake` 变量 / `PascalCase` 类型。
- 所有 Win32 调用失败路径必须写日志或静默跳过（不得崩溃）。
- 新增策略逻辑必须同步加入 `/selftest`。
- 不引入第三方运行时；不新增网络/驱动/服务。

## 7. 发布检查清单

- [ ] `build.cmd test` PASS，零警告
- [ ] `/selftest`、`/dryrun` 目录正确（保护名单不出现）
- [ ] 手动清理前后指标与保护验证
- [ ] 托盘/热键/主题/系统 Sheet/导出诊断
- [ ] 主屏/设置/系统 Sheet Private < 20MB，Ping < 50ms（150% DPI）
- [ ] Sheet 200ms 后无额外位图/定时器，空闲无持续 60fps
- [ ] 安装版 SHA256 与 dist 一致
- [ ] README/CHANGELOG/RESEARCH/ENGINEERING 更新
