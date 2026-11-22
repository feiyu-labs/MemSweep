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
3. 自动模式：常规压力「空闲后台修剪 + 低优先级缓存」可同次执行；当出现真实枯竭
   （可用 < 512MB 或使用率 ≥ 95%）或空闲页严重饥饿（`free < 64MB`、`P0 < 32MB`、
   总待机 `≥ 2GB`、负载 `≥ 阈值+5`、硬缺页 `< 1500/s` 且距上次全量清理 ≥ 10 分钟）时，
   才升级全量待机缓存清理；仅真实枯竭允许非空闲后台修剪。高压缩压力（压缩 ≥ 20% 物理内存
   或提交 ≥ 85%）时，对高回弹进程自动将重修剪冷却翻倍。自动模式永不合并页面。
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
    不得使用绝对承诺（“零卡顿”“无感”）。开关副文案只有 `r.Width - 58` 的宽度，
    统一用省略号收尾；新增或改写的副文案必须中英各渲染一张确认没被截断。
14. **设置减法**：高级设置固定 5 项；抖动保护内置护栏不暴露开关；
    页面合并跟随「清理力度=深度」，不设独立开关；**默认力度是「均衡」**（全量缓存与文件缓存开，合并页关），
    只有用户主动选「深度」才包含页面合并；「后台效率模式」在原开关下统一调度 CPU（动态可逆降级
    `BELOW_NORMAL`）/ `ProcessIoPriority` / `MEMORY_PRIORITY_LOW`，不新增开关；静默模式在设置页；
    新增设置项必须先证明不能并入现有项；主题/语言**只在标题栏**，设置页不得重复（v1.9.7）。
    设置页分组的组间距 ≥14px；新增一行前先确认还能维持这个呼吸感。
15. **原生控件占位符**：只要有一个原生 `Edit` 盖在 GDI+ 绘制层上，GDI+ 画的占位符就不可见——
    必须用 `EM_SETCUEBANNER`，并在语言切换时同步；`/uipng` 快照不能作为占位符的验收依据。
16. **清理完成反馈**：托盘气泡之外，主屏状态区必须显示 6s 结果（清理完成 + 释放/可用）；
    到期由 1s tick 清理并重绘。Toast 宽度按文本测量（200–`L.w-32`）、底部居中，
    不得用固定宽度；Windows 通知头部的应用名来自 `FileDescription`，必须短（MemSweep）。
17. **发布物完整性**：两个架构的 zip 必须包含 `MemSweep.exe`（统一名）、`install.cmd`、
    `uninstall.cmd`、`INSTALL.txt`、`LICENSE`；`install.cmd` 要同时支持 zip 根目录与 `dist\`；
    CI 必须编译 ARM64；release 的 `gh` 参数用显式文件列表，不依赖 shell 通配符。
    两个 `.cmd` 都是 UTF-8，开头必须 `chcp 65001 >nul`，否则中文提示在 936 控制台是乱码。
18. **布局完整性 / 模态绘制**：`BuildLayout` 必须为当前屏幕的**所有**绘制几何赋值，
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
| `/dryrun` | 只列出将修剪的进程和清理计划并写日志，不动作 |
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

- `Sheet::None / Settings / Advanced / Processes / System / About`；没有顶层 Page 枚举、没有顶部导航。
- 首页「系统状态」卡片与 System Sheet 共用 `CountSysWarnings()` 同一套告警口径；进程表通过 `g_procListDirty` 缓存，悬停重绘不重新排序。
- 主屏 4 组：状态句 + 细环 + 主按钮 + 两个文字入口；进程/系统按需打开。
- Sheet 是主屏上的一层覆盖：scrim + 200ms ease-out 位移；没有页面交叉淡入、没有页面位图。
- 设置 Sheet 单列、无 tabs、无保存按钮；修改即时 `CommitSettings()`（debounce/失焦/关闭时）。
- 高级设置是紧凑 Sheet；进程/系统 Sheet 按内容缩短高度，系统更多状态再展开。
- 原生 Edit 只有白名单一个；Sheet 动画期间隐藏，settled 后显示；切页/关闭时提交。
- 命中：Sheet 打开时只注册 Sheet 控件 + 四周 scrim；主屏/窗口按钮不可点。
- 性能：Sheet 高度 340–568 DIP；无页面过渡缓冲；空闲只跑 1s 指标刷新。
- **动效纪律**：动画步进只在 33ms 定时器（`StepAnimations`，帧率无关指数平滑）；
  环境动效（Timer 4，6.7fps）仅在 **窗口前台活跃（`g_wndActive`）+ 可见 + 主屏 + 非清理** 时运行，
  窗口失焦（`WM_ACTIVATE WA_INACTIVE`）立即 `KillTimer(4)`，隐藏/最小化立即 `KillTimer(1..4)` 做到 0 唤醒；
  禁止只判断 `IsWindowVisible`（被其他窗口盖住时仍为真，v2.0.5 实测 21 小时后台空绘吃掉 577s CPU）；
  尊重 SPI_GETCLIENTAREAANIMATION；禁止把动画推进写进数据刷新路径。
- **EcoQoS 与 Win11 定时器节流纪律**：开启效率模式时按位 OR 传入 `EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION (0x4)`
  （Win10 失败回退仅 `EXECUTION_SPEED`）；恢复时必须写 `ControlMask=0, StateMask=0` 交回系统管理，
  禁止写 `ControlMask=EXECUTION_SPEED, StateMask=0`（微软文档明确后者会把进程锁死强制 HighQoS）。
- **自动清理防空跑与预算纪律**：手动清理/离开维护完成后同步刷新 `g_lastAutoCleanMs`；
  `RunClean` 修剪循环的 2000ms/6000ms 预算必须从循环进入处独立计时（`trimT0`），不得被前置 `PurgeStandby` 耗光；
  `PlanAuto` 仅在 P0 待机页 ≥ 32MB 时派发低优清理，连续低收益（<64MB）按指数退避冷却（真实 OOM 可用 <512MB 不退避）；
  95% 负载但可用 ≥512MB 时全量待机清理仍遵守 10 分钟冷却。
- **可见窗/有声进程保护与 ProBalance 滞回**：`BuildProtectedPids(s, true)` 在清理和后台让路时按需收集
  Z 序未被遮挡的可见顶层窗（`DWMWA_CLOAKED` 过滤）与 WASAPI 活跃音频会话进程（`eConsole` + `eCommunications`），
  闲时零调用；动态 CPU 让路仅在用户活跃（`!IsUserIdle(30)`）且总 CPU ≥35%（单进程 ≥12%）或单后台 ≥25% 时介入，
  已让路进程设 15s 最短保持期再按 <4% 恢复，防止编译/GC 波动反复横跳。
- **动画只在主屏推进**：仪表缓动、光晕、涟漪、清理旋转仅当 `g_sheet == None` 时步进；
  Sheet 打开时这些元素在遮罩后，继续推进会让 33ms 定时器整窗重绘（清理中尤其浪费）。
  打开/关闭 Sheet 时把 `g_gaugeShown` 收敛到目标值，避免补播隐藏期间的动画。
- **清理结果数字来源**：完成后的「可用」用 `g_lastCleanAvail`（清理线程记录的 `availAfter`），
  不用监控快照（最多滞后 5 秒，会与「已释放」矛盾）。
- **关闭效率模式必须可回滚**：`UpdateProcessTracking` 在 `!cfg.ecoQoS` 时对残留的
  `ecoMarked`/`cpuRestrained` 做兜底恢复，覆盖「清理进行中关闭开关」的竞态。
- **清理候选地板只允许一处**：`RunClean` 的修剪/效率标记循环必须使用
  `floorMB = plan.minWSMB > 0 ? plan.minWSMB : c.minWSAutoMB`。自动清理无候选时
  `plan.minWSMB` 是 0，若直接拿它当阈值，会把几十 MB 的小进程也全部打上效率标记
  （v2.0.3 实测 212 → 30 个）。
- **重绘纪律**：`WM_PAINT` 按 `BeginPaint` 更新区做部分重绘（背景缓冲保留上一帧，
  `DrawAll(clip)` 仅栅格化失效区）；环境动效只失效仪表矩形；禁止全屏 Invalidate 的高频路径。
  **回缓冲必须是 DIB 段 + 兼容 DC（`BitBlt` 上屏），禁止用 GDI+ `DrawImage` 把 `Bitmap` 贴到窗口 DC**：
  后者每像素做格式转换，780×900 实测 12–34ms，`BitBlt` 为 0.7–1.6ms（v2.0.2）。
  **Sheet 动画期间只失效卡片滑动带**（`InvalidateSheetSlide`），遮罩恒定后卡片外像素逐帧相同。
- **遮罩不透明度禁止参与动画**：Sheet 打开时遮罩必须首帧到位；若让它从 0 淡入而卡片同时滑动，
  周围 16px 边距会先亮后暗，看起来就是「卡片后面闪一圈白框」（v2.0.2 实机抓帧确认并修复）。
- **绘制快照必须复制，不可 `std::swap`**：两次刷新之间可能没有新的监控发布（清理完成的
  `WM_APP_STATS/CLEANING` 会强制刷新），交换会把上一次换出去的旧快照换回来，出现
  「0 KB / 0 KB、可用 0 KB」而仪表仍为新指标（v2.0.2）。首个采样发布前不绘制任何数字。
- **原生编辑框显示顺序**：无滑入动画时先 `InvalidateRect` + `UpdateWindow` 画出 Sheet，
  再 `PositionEdits()` 显示 Edit；否则浅色编辑框会先闪在首页上。
- **托盘纪律**：图标只在整数百分比变化时重建（DIB+GDI+ 绘制开销大）；
  tooltip 变化走 `NIF_TIP` 轻量更新；`Shell_NotifyIcon` IPC 不进入每秒路径。
- **体积与容器纪律**：`-Oz + -fvisibility=hidden + -fvisibility-inlines-hidden + gc-sections + strip` 为该工具链实测最优，
  `build.cmd` 分离 `obj/app.res` / `obj/app_dev.res` / `obj/app_arm64.res` 避免目标互踩；`SaveConfig()` 用单块
  `swprintf` 缓冲替代 25 次 `std::to_wstring` 拼接；Sheet 绘制函数加 `noinline` 控制 `DrawAll` 帧膨胀；
  **全项目禁用 `std::unordered_map` / `std::unordered_set` / `std::deque`，避免多余 `std::sort` 实例化**：
  监控与保护数据统一收敛为按 `pid` 排序的 `PidTrack` 单表（`qsort` + `lower_bound`）、UI 动画用 8 项定长数组、
  UTF-8 读写与 `ForegroundPid()` 共用 `core.h` 实现（v2.0.1 实测比 v2.0.0 缩小 43 KB，x64 降至 398 KB）。
  已否决 `-falign-*`/`-fmerge-all-constants`（零收益）、unwind 表移除（CRT `.seh_` 编译失败）、
  `--icf`/`--build-id=none`/`--no-insert-timestamp`/`-stdlib=libc++`（不支持或无收益）、
  `-fno-threadsafe-statics`（省 512B，但静态初始化有并发风险）。ico 只保留
  16/20/24/32/48/256 六个关键尺寸；进程名统一走 `NormalizeProcName`。
- **标题栏控件清单**：新增标题栏按钮必须同步四处——`Layout` 矩形、`BuildLayout` 命中、
  `DrawChrome` 绘制、**`WM_NCHITTEST` 的 `HTCLIENT` 排除清单**（漏掉会被 `HTCAPTION`
  拖拽吞掉点击，v1.9.3 教训）。
- **标题栏图标规范**：统一 1.5px 圆头描边、16px 光学尺寸、30×30 悬停面；idle `T.text2`、
  hover `T.text`。主题/语言都显示“点击后”的状态（浅色→月亮、深色→太阳；中文→EN、英文→中），
  两个快切按钮不得一个显示当前、一个显示目标。只有窗口关闭按钮使用 danger 悬停色。
- **进程表单一入口**：查看内存、切换保护、释放内存都在同一张 Sheet，按应用名聚合显示总专用工作集与实例数；
  左键=按应用切换保护，右键=批量释放该应用全部非前台非受保护实例的内存；
  系统/VM 用锁标记且不可切换。不要再加第二个进程列表或独立保护名单 Sheet。
- **动态后台调度安全边界**：`MonitorTick` 中对高 CPU 后台进程的 `BELOW_NORMAL` + `IO_PRIORITY_LOW`
  让路必须仅在 `ecoQoS` 开启时执行，校验 `pid + createTime` 防复用，且在 CPU 回落、切回前台（`NoteFocus`）、
  关闭开关或程序退出（`RevertBackgroundTaming`）时立即恢复 `NORMAL_PRIORITY_CLASS` 与正常 I/O。
- **保护名单语义**：精确进程名（去 `.exe`、小写），不是关键字；输入框与进程表共用同一份
  `g_cfg.whitelist`，任一入口修改后另一入口必须同步（`SetWindowTextW`）。
- **品牌标记单一来源**：`DrawBrandMark`（256 网格、squircle r56、M 圆头折线 stroke 36）同时服务
  标题栏/关于页；`res/icon.ico` 的生成脚本必须使用同一组坐标与颜色。改标记必须同步两处，
  并重新生成六个关键尺寸（16/20/24/32/48/256）——不要只放一张 256 PNG 让系统缩放。

## 5.1 线程模型

- UI 线程：窗口消息、GDI+ 绘制、配置编辑；只做轻量读取。
- `MonitorThread`：首次采样也由线程内完成（`RunUI` 不再并发调用），等待 `monEvent + LowMemoryResourceNotification`（5s 超时，低内存事件 2s 节流）+ 采样与策略；
  每 tick 计算 `ComputeReclaimable`（估算主屏“可释放约 X”）；配置一律取 `GetConfigSnapshot()` 不可变副本；
  通过临界区发布 `Metrics`，UI 只做取整比较。
- `SysInfoThread`：`Get-MMAgent` / `Win32_PageFileUsage`（多盘页面文件用 `Measure-Object -Sum` 汇总），WSL 用 `LxssManager` 注册表键快速探测；
  在本地 `SysState` 上完成查询，`SRWLOCK` 一次性换入；完成后 `WM_APP_SYSINFO`（仅系统 Sheet 可见时刷新）。
- `SysActionThread`：确认 `MessageBoxW` 先在 UI 线程完成，`SysActionThread` 只做后台 PowerShell / `.wslconfig` 原子写与状态刷新，需要提示时走 `WM_APP_TOAST`；`WM_APP_SYSINFO`。
- `AutostartThread`：`schtasks` 创建/删除；`WM_APP_AUTOSTART`。
- `IdleDeepThread`（离开时维护）：空闲判定通过后取 `PlanIdle` 快照执行；
  独立 watcher 线程监视用户输入，`SetEvent(cancel)` → RunClean 修剪循环与页面合并同步中止。
- 任何后台线程不得直接触碰窗口句柄之外的 UI 状态；`HWND` 仅用于 `PostMessage`（`WM_APP_TOAST` / `WM_APP_BALLOON` 等）。

## 6. 代码风格

- 4 空格缩进、K&R 括号、120 列；UTF-8（无 BOM）；`snake` 变量 / `PascalCase` 类型。
- 所有 Win32 调用失败路径必须写日志或静默跳过（不得崩溃）。
- 新增策略逻辑必须同步加入 `/selftest`。
- 不引入第三方运行时；不新增网络/驱动/服务。

### 热键不变量

- 只允许通过 `ApplyHotkey()` 注册/注销；失败时回滚到上一个**确实注册成功**的组合
  （`g_hkValid` 记录真实状态，不能拿“配置里的值”冒充旧值）。
- 配置只在注册成功后写入，避免“配置是新键、系统里是旧键”的分裂。
- 录制期间**不注销**热键，改由 `WM_HOTKEY` 在 `g_hkCapture` 时吞掉触发，
  避免热键短暂失效的空窗；录制中切走窗口（`WM_KILLFOCUS`）必须放弃录制。
- 保留组合（`Alt+Tab`、`Alt+F4`、`Ctrl+S`、`Ctrl+Enter`、`Ctrl+Space`、`Esc`）由
  `IsReservedHotkey()` 统一拒绝，`ApplyHotkey()` 做最后把关，防止手改配置绕过界面。
- 组合键的解析/格式化是纯函数（`HotkeySpec`），改动必须同步 `/selftest` 用例。

## 7. 发布检查清单

- [ ] `build.cmd test` PASS，零警告
- [ ] `/selftest`、`/dryrun` 目录正确（保护名单不出现）
- [ ] 手动清理前后指标与保护验证
- [ ] 托盘/热键（改键、占用回落、Esc 取消）/主题/系统 Sheet/导出诊断
- [ ] 主屏/设置/系统 Sheet Private < 20MB，Ping < 50ms（150% DPI）
- [ ] Sheet 200ms 后无额外位图/定时器，空闲无持续 60fps
- [ ] 安装版 SHA256 与 dist 一致
- [ ] README/README.zh-CN 里的体积、占用与版本号都从本次 `dist` 实测更新，不沿用上一版
- [ ] winget 清单已按本次 tag 重新生成（`packaging/winget/make-winget-manifest.ps1 vX.Y.Z`），
  `winget validate --manifest <目录>` 通过；schema 1.28.0，文件名必须是 `<id>.locale.en-US.yaml`
- [ ] `docs/release-notes/<tag>.md` 已写：按 `_TEMPLATE.md` 的三条规矩——一句话概述 + 3–5 条用户可见变化、
  不写实现细节/包体/测试结果、英文在前中文在后；`release.yml` 优先用它，缺失才退回自动生成
- [ ] `src/common.h`、`res/app.rc`、`res/app.manifest`、`res/app_dev.manifest` 版本号一致
- [ ] README/CHANGELOG/RESEARCH/ENGINEERING 更新
