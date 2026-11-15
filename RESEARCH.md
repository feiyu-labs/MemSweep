# MemSweep 全链路调研复核报告

日期：2026-11-15 · 目标机：MECHREVO GM6AR57C / i7-12800HX / 23.74GB / Win11 26H2 Insider 26340

---

## TL;DR

1. **底层技术已「封顶」**：全网内存清理器都只是 5 个 Windows Native API 的包装，没有隐藏魔法。
   微软 26H2 正在系统级改进内存管理，清理器的正确价值是「治理失控进程」，不是对抗 OS。
2. **顶级工具都把「全量待机清理」与「脏页刷盘」列为冻区**：Mem Reduct 自动清理直接排除
   standby/modified；Process Lasso SmartTrim 的 Purge Standby List 默认关闭；
   ISLC 则要求「standby ≥ X 且 free < Y」双条件。v1.7 只在真实枯竭
   （可用 < 512MB 且空闲 < 128MB）才允许全量清缓存，常规高压只清低优先级。
3. **修剪工作集才是用户可见的杠杆**：本机实测 116 个后台进程修剪 → 可用 +1128MB，空闲 +3509MB / 4.1s。
   待机清理只把 standby 变成 free，**不改变任务管理器「可用%」**（可用 = 空闲 + 待机）。
   因此 v1.7 的效果统计改为优先显示「空闲 +X」，而不是用可用差冒充“释放”。
4. MemSweep 的差异化 = 前台免疫 + 空闲感知 + 修剪记忆 + 指标门控 + 诚实 UI + 零刷盘，
   这套组合在同赛道没有完整对标。

---

## 一、调研方法

- **一手源码**：Mem Reduct 3.5.2（GitHub main.c / main.h，11,245★）、WinMemoryCleaner 3.0.8、
  RAMKeeper 1.1.0、WinMemClean、MemListMgr、Process Lasso 文档。
- **系统头文件**：phnt（SystemInformer）`ntexapi.h` / `ntmmapi.h`，用于核对 NT 常量与结构体布局。
- **微软官方文档**：EmptyWorkingSet、SetProcessInformation（ProcessPowerThrottling / MemoryPriority）、
  GlobalMemoryStatusEx、CreateMemoryResourceNotification、Task Manager 内存口径。
- **社区**：Reddit r/Windows11、linux.do（2114958、3071883、2169528）、V2EX（1142023、1254672）、
  HN 37739253、Bitsum SmartTrim 文档。
- **现场探针**：自写 PowerShell + C# 探针，在目标机验证类 80 查询、进程快照布局、
  EmptyWorkingSet、MemoryPriority、EcoQoS、Purge 权限错误码（见下）。

---

## 二、竞品全景（2026-11）

| 工具 | 版本/热度 | 机制 | 致命短板 |
|---|---|---|---|
| Mem Reduct | 3.5.2 / 11,245★ | WS + 文件缓存 + 低优先级待机 + 合并页；自动阈值 90%，冷却 30s | 无进程排除；全局一刀切（含前台）；默认刷注册表/修改文件缓存 |
| WinMemoryCleaner | 3.0.8 / 顶流 | 7 区域可选、进程排除、热键、托盘% | 自动触发语义是「可用% < X」→ 死循环；默认含修改页/文件缓存（卡死元凶）；WPF 30-50MB |
| Process Lasso SmartTrim | 19.x 闭源 | 双条件门控 + 优先级 0 待机 + 可选性能模式 | 闭源、重、Pro 才可细调 |
| ISLC | 1.0.3.7 | 仅清待机 + 排除列表 + 定时器分辨率 | 不碰工作集；闭源 |
| RAMKeeper | 1.1.0 / 163KB C++ | 每进程 EmptyWorkingSet + 刷修改页 + 清待机 | 无前台/空闲智能；**刷修改页** |
| WinMemClean | Rust CLI 2026 | 默认排除 freeze-prone 区域 | 纯 CLI，无托盘 |
| WinMemoryOpt | C# 2026 | EcoQoS、前台跟踪、定时 | .NET 常驻偏重 |
| MemListMgr | VB6 / MIT | RAMMap 类手动工具（含合并页/刷缓存） | 手动、无守护 |
| AmazeMem | 2026 | SYSTEM 计划任务 + 内核 purge | 含修改页刷盘（风险） |
| CleanMem | 2.5.0 / 2021 | 计划任务每 15 分钟 | 停更；作者自述不会更快 |

---

## 三、底层 API 一手核验（phnt ntexapi.h）

```
SystemMemoryListInformation = 80      // 需 SeProfileSingleProcessPrivilege
  MemoryEmptyWorkingSets          = 2 (未使用：全局一刀切会碰前台)
  MemoryFlushModifiedList         = 3 (禁用：脏页刷盘)
  MemoryPurgeStandbyList          = 4 (全量清待机，仅手动/紧急)
  MemoryPurgeLowPriorityStandbyList = 5 (温和清待机)
SystemFileCacheInformationEx = 81     // 需 SeIncreaseQuotaPrivilege
SystemProcessInformation     = 5      // 零句柄快照
SystemCombinePhysicalMemoryInformation = 130 (合并页，未采用)
SystemRegistryReconciliationInformation = 155 (注册表刷盘，禁用)
```

结构体布局（x64）已用 `static_assert` 固定并在探针中验证：
`SYSTEM_PROCESS_INFORMATION` 的 `UniqueProcessId@80 / SessionId@100 / WorkingSetSize@144 /
PagefileUsage@184` 全部正确；`SYSTEM_MEMORY_LIST_INFORMATION`（22×SIZE_T）查询免提权可用。

**探针实测（目标机）**

| 能力 | 结果 |
|---|---|
| 类 80 查询 | ✅ 免提权，返回零页/空闲页/待机 8 优先级分桶 |
| 进程快照 | ✅ 397 进程，字段正确 |
| EmptyWorkingSet | ✅ 5.6ms，WS 8MB→0 |
| MemoryPriority=LOW | ✅ 设置成功、读回=2 |
| EcoQoS | ✅ 设置成功（结构体必须含 Version=1） |
| 无提权 Purge | ✅ 返回 0xC0000061（权限拒绝，符合预期） |
| 系统文件缓存（无提权） | ✅ 返回 0xC0000022（拒绝） |

---

## 四、四个关键结论（决定了最终设计）

1. **「使用中%」与「可用%」是两套口径**：修剪 WS 降低使用中、提高可用；
   清待机只在「可用」内部搬运（待机→空闲），不改变可用%。因此自动模式必须以 WS 修剪为主。
2. **全量待机清理是冻区**：Mem Reduct 自动清理排除它；SmartTrim 用双条件控制它。
   故自动模式只用低优先级清理（且需空闲地板 + 待机地板双条件），全量仅手动。
3. **硬缺页尖峰不能阻断 WS 修剪**：本机长期 17,914 页/秒，若阻断则永不清理；
   尖峰只应门控「待机清理」，因为 WS 修剪恰恰能缓解颠簸。
4. **修剪与清待机不能背靠背**：手动顺序必须是「待机 → 文件缓存 → 修剪」，
   否则刚修剪出的 standby 页会被立刻丢弃，恢复时硬缺页（卡死配方）。

---

## 五、实测数据（目标机）

### 5.1 清理效果

| 测试 | 结果 |
|---|---|
| 手动清理（未提权，仅 WS） | 候选 148 → 修剪 124 → **可用 3.6→5.6GB（+2059MB）/ 1.6s** |
| 手动清理（第二次） | 可用 869→1403MB（+534MB），仅修剪 2 个大进程（2s 预算） |
| 自动守护 | 91% 负载触发；空闲阈值未满时候选 0，不误伤 |
| 自身内存 | 3.1MB WS（含 2.7MB 双缓冲）；每秒统计刷新 0.0% CPU |

### 5.2 保护验证

- 前台 AyuGram 782MB：清理前后 **782MB → 782MB（不变）**
- 白名单 claude 2514→2524MB、zcode 2210→2227MB（正常波动，未修剪）
- MsMpEng / ZCode / Claude / Hermes 在 /dryrun 候选中**全部消失**（`.exe` 后缀匹配 bug 修复后）

### 5.3 指标口径交叉验证

`Available ≈ 空闲(Zero+Free) + 待机(8 优先级之和)`，与性能计数器
`Free & Zero Page List Bytes + Standby Cache Normal/Reserve/Core` 一致；
`使用中% = 100 − 可用/总量` 与任务管理器一致。

---

## 六、MemSweep 设计决策（含 6 项复核修正）

| # | 原设计 | 问题 | 修正 |
|---|---|---|---|
| 1 | 自动每次都清低优先级待机 | 本机低优先级待机≈0；且不改变可用% | 自动以 WS 修剪为主；待机清理需双条件且仅当无修剪候选 |
| 2 | 修剪后立即清待机 | 丢弃刚修剪的页 → 硬缺页 | 手动顺序反转为「待机→缓存→修剪」；自动两者互斥 |
| 3 | 硬缺页尖峰推迟全部清理 | 本机 17,914/s 会永不清理 | 尖峰只门控待机清理；不阻断 WS 修剪 |
| 4 | EcoQoS 结构体缺 Version | 微软要求 `Version=1` | 已补；措辞改为「引导使用能效核」而非强制 |
| 5 | 每 180s 重复修剪同一批 | 反复回填抖动 | Per-process 修剪记忆：10 分钟内不重复，除非 WS 增长 >1.5× |
| 6 | 无会话隔离 | 管理员会碰其他会话 | 默认仅当前会话（`allSessions=0`） |

另外新增：类 80 指标显示（空闲/待机/修改页）、`/dryrun` 演练、`/selftest` 自检、
手动清理 6s 预算、清理线程最低优先级、双缓冲无闪烁 UI、三档主题。

---

## 七、安全与失败模式

| 风险 | 缓解 | 验证 |
|---|---|---|
| 卡死（WMC 同款） | 正确触发语义 + 冷却 + 双条件门控 + 无脏页刷盘 + 时间预算 | 1.6s 清理无冻结 |
| 误伤前台 | 焦点 + 父链 + 120s 焦点历史 + 白名单 | 前台 WS 前后不变 |
| PID 复用误判 | CreateTime 校验 | 代码审查 |
| 其他用户进程 | 默认仅当前会话 | 代码审查 |
| 无权限半残运行 | 清单 requireAdministrator；无权限明确退出 | `/clean` exit=2 |
| 热键冲突 | 注册失败写日志；菜单/中键兜底 | 日志验证 |
| 杀软误报 | 无网络/注入/驱动；提供 SHA256 | — |

**永不做**：脏页刷盘、注册表刷盘、全局一刀切、定时器分辨率 hack、结束/挂起进程、联网/遥测。

---

## 八、质量门禁结果

- ✅ `-Wall -Wextra` 零警告（发布构建）
- ✅ `/selftest` 全部通过（触发语义/冷却/双条件/手动计划/格式化）
- ✅ `/dryrun` 保护名单正确（MsMpEng/ZCode/Claude/Hermes 不出现）
- ✅ 手动清理实测 124 进程 / +2059MB / 1.6s
- ✅ 单实例转发（`/clean` → 运行中 UI）
- ✅ `/exit` 正常退出，无残留进程
- ✅ 窗口 1290×1170 @150% DPI，布局无重叠
- ✅ 浅色/深色/自动三主题离屏渲染验证
- ✅ 提权部署：安装到 Program Files、计划任务（HighestAvailable / 电池可启动 / 无限时限）
- ✅ 提权清理：候选 86 → 修剪 85 + 全量待机 + 文件缓存 → **释放 2294MB，可用 7.7→9.9GB，空闲 3.6→7.9GB，2.6s**
- ✅ 热键：Ctrl+Shift+M 触发清理（候选 17，空闲 1.1GB→6.4GB）
- ✅ WinMemoryCleaner：进程停止、计划任务禁用（可回滚）
- ✅ 安装文件 SHA256 与 dist 一致：`6425d730c82c95ef9aa350b01a3a8b9ffeebe71db7159d2046005f59f9a59b02`

---

## 九、参考

- phnt (SystemInformer): https://github.com/winsiderss/phnt
- Mem Reduct: https://github.com/henrypp/memreduct （main.h 中 `REDUCT_MASK_FREEZES`）
- WinMemoryCleaner: https://github.com/IgorMundstein/WinMemoryCleaner
- Process Lasso SmartTrim: https://bitsum.com/apps/process-lasso/docs/algorithms/smarttrim/
- RAMKeeper: https://github.com/ashishpatel26/RAMKeeper
- WinMemClean: https://github.com/nusu-github/WinMemClean
- MemListMgr: https://github.com/fafalone/memlistmgr
- ISLC: https://www.wagnardsoft.com/forums/viewtopic.php?t=1256
- EmptyWorkingSet: https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-emptyworkingset
- SetProcessInformation: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
- CreateMemoryResourceNotification: https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-creatememoryresourcenotification
- Windows 11 26H2 内存改进（微软官方数据报道）: https://blogs.windows.com/windowsexperience/2026/11/03/raising-the-bar-for-8-gb-windows-11-pcs-whats-improved-and-how-to-get-it/
- linux.do 讨论: https://linux.do/t/topic/2114958 · https://linux.do/t/topic/3071883
- V2EX 讨论: https://www.v2ex.com/t/1142023 · https://v2ex.com/t/1254672
- Hacker News（WS 修剪语义）: https://news.ycombinator.com/item?id=37739253


---

## 十、Windows 自带内存机制审计（2026-11-15 补充）

| 机制 | 本机状态 | 结论 |
|---|---|---|
| 内存压缩 MemoryCompression | 已开启 | 保持；实测压缩存储 1.6GB、压缩率 2.60× |
| 页面合并 PageCombining | 已开启 | 保持（相同物理页去重） |
| 预取/预启动 | 已开启 | 保持（SysMain 一部分） |
| SysMain | 运行中 | 保持（SSD 上收益为正；社区"关闭提速"说法多为个例） |
| 快速启动 | 已关闭 | 保持关闭（避免驱动/内核池跨"关机"残留） |
| 页面文件 | D: 固定 16→24GB / 48→64GB | 已调整；位于 Samsung 980 PRO；重启生效 |
| WSL | 未启用 | 若启用：`.wslconfig [wsl2] autoMemoryReclaim=gradual` |
| 非分页池 | 829MB | 健康；新增 >1GB 橙色预警 |
| 提交 | 39.8/42.4GB (94%) | 页面文件调整后上限提升；重启后回落 |

**新特性适配**：压缩存储查询（`SystemStoreInformation=109 / MemCompressionInfoRequest=22`，
实测可用）、内核池监控（`GetPerformanceInfo`）、提交率告警、WSL 自动回收配置。

> 说明：本轮补充调研时本机外网仅百度可达（GitHub/微软文档/v2ex/linux.do 均 TLS 重置），
> 实时社区复查未完成；以上结论交叉核对了本会话已抓取的一手资料、2026-11-13 系统精简规划
> 与本地提权审计数据。


---

## 十一、v1.1 打磨与“打开闪退”复盘（2026-11-15）

**现象**：双击 exe 闪一下、无界面；窗口 `IsWindowVisible=true` 但被全屏游戏遮挡；真实窗口客户区 1268×1114（窗口 1290×1170），出现原生标题栏 + 自定义标题栏双标题。

**根因与修复**

| # | 根因 | 修复 |
|---|---|---|
| 1 | 托盘左键是"显示/隐藏"切换，双击 = 显示→隐藏 | 左键永远打开面板 + 250ms 去抖；双击/重复事件不再隐藏 |
| 2 | 手动启动（无 `/tray`）也静默进托盘 | `showOnStart = !/tray`；计划任务仍带 `/tray` |
| 3 | `WS_OVERLAPPEDWINDOW` + 只处理 `WM_NCCALCSIZE(wParam=TRUE)` → 原生边框未去 | 改用 `WS_POPUP\|WS_THICKFRAME\|WS_MINIMIZEBOX` + `WM_NCCALCSIZE` 两种 wParam 都返回 0；client=window |
| 4 | 全屏游戏抢占前台，`SetForegroundWindow` 失败 | `AttachThreadInput` 强制前台 + TOPMOST↔NOTOPMOST + 任务栏闪烁 + 被遮挡时气泡提示 Alt+Tab |
| 5 | 窗口位置默认在左上角 | 按工作区居中（主窗口与系统检查窗口） |
| 6 | 双击标题栏会最大化 | 拦截 `WM_NCLBUTTONDBLCLK` |

**自动修剪“候选恒 0”修复**

- 原判定：任意 100ns CPU 增量都会重置空闲计时 → 聊天/浏览器/IDE 的毫秒级心跳让大进程永不空闲。
- 新判定：仅当 CPU 增量 > 单核 0.5%（`dtMs*50` 个 100ns）才算活动；高压（负载 ≥ 阈值+5 或空闲 <512MB）时空闲阈值自动放宽到 60s。
- 实测：临时 70% 阈值下，第 2 次触发即 **候选 2 / 修剪 2 / 释放 196MB**。

**验证结果**

- 主窗口 client = window = 1290×1170 @150%，无原生标题栏；`foreground-is-main=True`
- 系统检查窗口 960×1050 @150%，同样无边框、居中
- 主题点击：深色→浅色均写入 config 并生效
- 子控件：4 个编辑框存在且白名单文本正常
- 启动行为：手动可见 / `/tray` 隐藏 / `/show` 前置，三项全部通过
- 安装版 SHA256：`12b21fc5266dc6dcaa58848a08debb0603c8f546908107b4d71dad018f3c292a`


---

## 十二、v1.2 工程优化与 WinMemoryCleaner 清除（2026-11-15 第二轮）

### 1. WinMemoryCleaner 彻底移除

- 进程：无；计划任务：已删除；winget 包目录：已删除；winget 链接：已删除；
  ARP 卸载项（HKCU Uninstall）：已删除；`HKLM/HKCU\SOFTWARE\WinMemoryCleaner`：已删除；
  桌面/开始菜单快捷方式：已删除；深度搜索（AppData/ProgramData/Program Files/Desktop）无残留。
- `winget list --id IgorMundstein.WinMemoryCleaner` → “找不到与输入条件匹配的已安装程序包”。

### 2. 内存占用：65MB → 8.8MB（关键发现）

- 对比实验：托盘隐藏 5MB → 窗口显示后 **65MB**；模块列表显示
  `ChatterflyPY.ime 27MB + ChatterflyTSF.ime 5.2MB + Resource.dll 4.4MB + voice_input 3.1MB`
  被系统在前台激活时注入。
- 修复：对 MemSweep 自己的窗口与编辑框调用 `ImmAssociateContextEx(..., 0)` 解除 IME/TSF 关联
  （进程名/数字输入无需中文），**私有提交 8.8MB、WS 9.5MB、IME 模块 0、句柄 759→184**。
- 其他：托盘图标仅在百分比变化时重建（Tooltip 32/64MB 阈值）、隐藏 20s 释放双缓冲、
  GDI+ 字体缓存、`g_trimRec` 随进程清理。

### 3. 工程规范

- 告警集扩展并保持零警告：`-Wshadow -Wformat=2 -Wundef -Wpointer-arith`。
- 新增 `build.cmd test`（dev + `/selftest` + `/idlecheck` + `/uipng` + `/checkpng`），实测 PASS。
- 新增 `.editorconfig`、`.clang-format`、`CHANGELOG.md`、`docs/ENGINEERING.md`。
- 崩溃过滤：`SetUnhandledExceptionFilter` 记录 `CRASH exception/addr`。
- 版本升级 v1.2.0.0；单文件 346KB。

### 4. 功能

- 暂停/恢复自动守护：托盘菜单 + 标题栏“已暂停” + CLI `/pause` `/resume`（已实测）。
- 键盘：`Esc` 隐藏、`Ctrl+Enter` 清理、`Ctrl+S` 保存。
- 系统检查新增“导出诊断”（桌面 `MemSweep-diag-*.txt`，含指标/系统/配置/日志尾部）。
- 系统检查 6 按钮 + `WM_DPICHANGED`；主窗口/系统检查均无边框居中。

### 5. 部署复核

- v1.2 最终冒烟：候选 136、修剪 136、全量待机✔、文件缓存✔，释放 **1998MB**，
  可用 3.6→5.5GB，空闲 879MB→3.6GB，3.3s。
- `/selftest` exit=0；`build.cmd test` PASS；安装版 SHA256 与 dist 一致：
  `12b21fc5266dc6dcaa58848a08debb0603c8f546908107b4d71dad018f3c292a`。
- 页面文件 `d:\pagefile.sys 24576 65536` 已直接写注册表并稳定（重启生效）。
- `Get-MMAgent`：MemoryCompression=True、PageCombining=True、ApplicationPreLaunch=False（系统现状）。


---

## 十三、v1.3 社区复查与深度整理（2026-11-15 第三轮）

### 1. GitHub 最新生态（2026-11-15）

| 项目 | 星标 | 最新 | 备注 |
|---|---|---|---|
| Mem Reduct | 11,247 | v3.5.2 (2025-04) | 仍是赛道标杆；无新版本 |
| WinMemoryCleaner | 5,353 | 3.0.8 | 已从本机彻底移除 |
| H3d9/memory_cleaner | 376 | 2026-11 | Memory Cleaner 重制版：全清理方法 + 等待链（游戏向） |
| DosX-dev/MemCleaner | 225 | 2026-10 | C，40ms 间隔逐进程 EmptyWorkingSet |
| Y-ASLant/MemoryCleaner | 51 | 2026-11 | Rust/GPUI，含清理历史、热键录制、低内存通知触发 |
| WinMemoryOpt | — | v1.2.3 (2026-11-06) | C#，EcoQoS/定点修剪/事件日志分析 |
| RAMKeeper | 4 | v1.1.0 | 142KB，托盘 |

**结论**：赛道新增工具大多在复刻既有 API；我们的“前台免疫 + 空闲感知 + 防抖 + 指标门控 + 系统检查”
组合仍无完整对标。对手值得借鉴的 UX：清理历史、热键录制、低内存通知触发、进程排除选择器。

### 2. 官方文档修正：WSL 自动回收

微软 WSL 配置文档明确：`autoMemoryReclaim` 位于 **`[experimental]`** 段，取值
`disabled / gradual / dropCache`（默认 `dropCache`）。此前代码写入 `[wsl2]` 是错误，v1.3 已修正；
系统检查提示也改为 `[experimental] autoMemoryReclaim=gradual`。

### 3. 页面合并（Combine Pages）实测与产品化

`NtSetSystemInformation(SystemCombinePhysicalMemoryInformation=130,
MEMORY_COMBINE_INFORMATION_EX)`，需 SeProfileSingleProcess/SeDebug/SeIncreaseQuota 特权。

| 场景 | 合并页数 | 释放 | 耗时 |
|---|---|---|---|
| 独立进程首次 | 698,775 页 ≈2.7GB | +2894 MB | 11.6s |
| 独立进程再次 | 293,075 页 ≈1.1GB | +948 MB | 7.9s |
| 运行实例转发 | 89,516 页 ≈350MB | +29 MB | 5.0s |

结论：收益可观但耗时 5–15s（后续实测可达 52.6s）→ v1.3 做成独立“深度整理”操作（托盘/CLI）；
  v1.7 进一步改为高级设置可选、默认关闭，自动模式永不合并。

### 4. 精准进程操作

- Top5 右键：修剪此进程 / 加入白名单（保护进程置灰）。
- CLI `/trimpid <PID>`；实测子进程 WS **7.5MB → 0MB**。
- 内部复用 `TrimPid` 与 `IsProtectedName`（内置名单 + 白名单 + self）。

### 5. 页面文件状态（重启后核实）

机器于 **10:37:55 重启**，页面文件按 24576 MB 初始大小重建（文件 25.77GB），
`Win32_PageFileUsage.AllocatedBaseSize=24576`；注册表 `PagingFiles=d:\pagefile.sys 24576 65536`。
**24–64GB 已实际生效，无需再次重启**（回滚 16384/49152）。

### 6. 最终状态

- 版本 v1.3.0.0，单文件 355,328 字节；SHA256
  `12b21fc5266dc6dcaa58848a08debb0603c8f546908107b4d71dad018f3c292a`。
- `build.cmd test` PASS；`/selftest` exit=0；深度整理/TrimPid 均已实测。

---

## 十四、v1.4 稳定性复核与“卡死/闪烁”根因（2026-11-15 第四轮）

### 1. 结论：不是技术栈选错

- 单文件 Win32/GDI+ 版本 **350KB**，托盘私有 **5.1MB**、面板 **11.4MB**；
  Electron/WinUI 同类常驻通常 50–150MB，且引入 WebView/运行时更新面。
- 真实根因是 **UI 线程架构**，不是语言：
  1. `Get-MMAgent` / `Get-CimInstance Win32_PageFileUsage` /
     `Get-WindowsOptionalFeature` / `schtasks` 同步跑在消息循环里；
  2. 全窗 60fps 重绘 + ClearType 在 150% 分数缩放下产生彩边/闪烁；
  3. 系统检查窗口未禁用 IME，Chatterfly TSF 注入约 56MB；
  4. `g_sys` 被工作线程直接写、UI 线程同时读，`std::wstring` 字段存在数据竞争，
     可能造成偶发闪烁/崩溃。
- 修复路径：工作线程 + `PostMessage` 回填；`g_sys` 改为本地快照 + `SRWLOCK` 一次性换入，
  绘制侧共享锁；浅层指标 4s；33ms 动画仅在动画/清理时运行；
  `AntiAliasGridFit` + `WS_CLIPCHILDREN`；系统检查窗口 `ImmAssociateContextEx`。
  这比换 Electron/WinUI 更符合“低占用、单文件、零依赖”的目标。

### 2. 实测（150% DPI）

| 状态 | Private | WS | Handles | 备注 |
|---|---|---|---|---|
| 托盘隐藏 | **5.1MB** | 4.3MB | 159 | 隐藏 20s 释放双缓冲 |
| 面板显示 | **11.4MB** | 17.1MB | 180 | 含 1290×1170 双缓冲 |
| 系统检查打开 | **15.4MB** | 22.2MB | 187 | 修复前 71.8MB（IME 注入） |
| 系统检查关闭 | **11.5MB** | 18.4MB | 183 | Chatterfly 不再常驻 |
| 清理后 | **11.8MB** | 10.7MB | 185 | `EmptyWorkingSet(self)` |

窗口消息 Ping 0ms；系统检查窗口创建约 111ms；`build.cmd test` PASS。

### 3. SysMain 被禁用是“页面合并关闭”的根因

- 本机检查：`Get-Service SysMain` → **Stopped / Disabled**；
  `Get-MMAgent` → `MemoryCompression=False, PageCombining=False, OperationAPI=False`。
- 恢复：`Set-Service SysMain -StartupType Automatic; Start-Service SysMain`，
  随后 `Enable-MMAgent -PageCombining -MemoryCompression` → 三项均 True。
- 系统检查的“启用压缩/启用页面合并”现在会先确保 SysMain 自动并启动，
  避免服务被禁用时 `Enable-MMAgent` 静默失败。
- 不采纳“为省内存禁用 SysMain”的社区 tweak：SSD 上页面合并/压缩收益为正，
  且禁用会让系统机制整体失效。

### 4. DPI 虚拟化导致的“内容只占左上角”

- dev 构建的 `res/app_dev.rc` 原只有图标，没有清单 → 进程 DPI-unaware，
  150% 下窗口 1290×1170 但内容按 860×780 绘制，右侧/底部空白。
- 新增 `res/app_dev.manifest`（asInvoker + PerMonitorV2）后，
  主窗 1290×1170、系统检查 960×1050 完整铺满；正式版原本就有 PMv2 清单。
- 说明：这是 dev 测试链路问题，不是安装版回归；现已修复并纳入构建。

### 5. 竞品 UX 取舍

- **保留/值得做**：清理历史（最近 7 次原因/释放/耗时）、热键录制、
  低内存通知触发、进程排除选择器；Top5 右键白名单已覆盖“排除”主路径。
- **不做**：定时器分辨率 hack、默认全量清待机、禁用 SysMain、脏页强刷盘、
  结束/挂起进程、常驻内核驱动。
- 下一项优先级最高的 UX：**清理历史**（透明、可审计、低风险）；本版先保证稳定性，
  不在同一版里叠加新交互。

### 6. 最终状态

- 版本 **v1.4.0.0**，单文件 **358,912 字节**；SHA256
  `97A08650D8E1AF1CA6CF780FF6EA9C5EE39C4F69245B3D0C6284866FBC195286`。
- 安装位置 `C:\Program Files\MemSweep\MemSweep.exe`；SysMain Running/Automatic，
  MemoryCompression/PageCombining=True；页面文件 24576–65536（allocated 24576）。
- `build.cmd test` PASS；安装版 `/selftest` PASS；系统检查/清理/截图均通过。

---

## 十五、v1.5 界面重构与技术栈复核（2026-11-15 第五轮）

### 1. 技术栈结论：Tauri 不是这个产品的“升级”

2026-10/11 的实测与社区反馈：

| 方案 | 安装包 | Windows 常驻内存 | 结论 |
|---|---:|---:|---|
| Tauri 2.12 + WebView2 | 1–15MB | 40–211MB；托盘隐藏可留 113MB private/310MB WS | 安装包小，但常驻不轻 |
| Electron | 85–200MB | 150–450MB | 不适合 |
| Slint | 数 MB | 30–95MB（软件渲染可更低） | 跨平台才考虑 |
| egui | ~10MB | 100–185MB | 不适合 |
| WinUI 3 | 10–30MB | 155MB+（复杂应用） | 不适合托盘工具 |
| **C++/Win32 + GDI+** | **0.36MB** | **8.8MB private / 14.8MB WS** | **保留** |

依据：
- Dawloom 2026-10 Windows benchmark：Tauri 2.12 空窗 142MB private / 333MB WS，接近 Electron。
- Tauri issue #16528：Windows 托盘应用隐藏后 WebView2 助手进程仍留约 113MB private / 310MB WS。
- V2EX 截图工具迁移：Tauri 后台 500–600MB → Qt Widgets + Rust 约 9.7MiB 私有工作集。
- linux.do「Winc」讨论：Tauri 2 常驻约 100MB，优化后仍难下探。
- Microsoft DirectComposition 文档：动画在合成线程运行，未来若需要更强过渡可加 D2D/DWrite + DComp，
  但不需要 WebView。

**结论**：继续 C++/Win32 + GDI+；560×660 单窗口先用 GDI+ 双缓冲，实测过渡足够；
若未来帧率不足，再评估 Direct2D/DirectWrite + DirectComposition。Tauri 的“先进”是跨平台与 Web 开发效率，
不是低内存。

### 2. 黑边根因

诊断（150% DPI，独立系统窗）：

- `GetWindowRect` = `GetClientRect` = 960×1050（内容没有裁切）。
- `style=0x94060000`：`WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_THICKFRAME | WS_MINIMIZEBOX`。
- `exstyle=0x100`：`WS_EX_WINDOWEDGE`。
- `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` 仍为 960×1050。

因此黑边不是绘制背景，而是 **原生厚边框/WindowEdge 的 DWM 合成**。v1.4.1 先移除 `WS_THICKFRAME`、
清除 `WS_EX_WINDOWEDGE` 并尝试 `DWMWA_BORDER_COLOR=NONE`，屏幕捕获确认消失；v1.5 直接把系统检查
并入主窗口，独立窗与黑边一起删除。

### 3. v1.5 信息架构

- 560×660 单窗口三页：概览 / 设置 / 系统。
- 概览：仪表盘 + 3 指标 + 主按钮 + 上次清理 + 自动守护摘要 + Top5。
- 设置：四个子页（自动 / 清理 / 保护 / 通用），一次一组；主题/语言/自启/热键/EcoQoS/静默收进设置。
- 系统：原系统检查内容变成同窗页面；不再有第二个顶层窗口/焦点模型。
- 页面过渡：旧页离屏捕获，180ms smoothstep 淡出；新页位移淡入；16ms 定时器只在过渡期间；
  过渡期间隐藏原生 Edit，结束恢复。
- 设置页底部说明卡：消除空白并解释策略边界；语言行与说明卡不再重叠。

### 4. 性能修复

- WSL 状态从 `Get-WindowsOptionalFeature`（DISM，慢）改为 `LxssManager` 服务注册表键快速探测。
- 系统页仍用工作线程查询，主窗口 Ping 0ms；页面首次进入约 1–2s 回填。
- 状态锁：`g_sys` 本地快照 + `SRWLOCK` 换入，绘制侧共享锁；避免 wstring 边写边读。
- 页面过渡按墙钟时间计算（180ms），不会因重绘繁忙而拖长。

### 5. 最终状态

- 版本 **v1.5.0.0**，单文件 **359,424 字节**；安装版 SHA256
  `9AFAFB22BFE3A9EC75E9B35843927E67EC31C8B82098D5059A7A485AB846489C`。
- 窗口 840×990 @150%（560×660 逻辑），单窗口 client=window。
- 概览/系统页 **8.8MB private / 14.8MB WS**；`build.cmd test` PASS，`/selftest` PASS。

### 6. 来源

- Tauri WebView2: https://v2.tauri.app/reference/webview-versions/
- 2026-10 Tauri/Electron/WPF Windows benchmark: https://dawloom.com/blog/tauri-vs-electron-vs-wpf-benchmark/
- Tauri tray WebView2 进程未释放: https://github.com/tauri-apps/tauri/issues/16528
- V2EX 截图工具 Tauri 500–600MB → Qt+Rust 9.7MiB: https://v2ex.com/t/1247135
- linux.do Tauri 常驻约 100MB 讨论: https://linux.do/t/topic/3121963?page=3
- Rust GUI 2026 生态与内存: https://goldstrikearch.github.io/rust-gui-desktop-ecosystem-state/
- Microsoft DirectComposition: https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-portal

---

## 十六、v1.6「One Screen」与 v1.7 清理策略（2026-11-15 第六/七轮）

### 1. 用户反馈与自查

反馈：“UI/UX 应该做减法，层级越少越清爽；当前内存被遮挡，右边参数大量空白。”

清点 v1.5.1 现状：

- 概览页约 **61 个绘制元素、12 个命中目标**；全应用 **43 个交互 ID**。
- 导航深度 = 3 顶层页 + 4 设置子页 = **7 个切换目标**。
- 设置页 **25 个控件**、7 段解释文字、4 张说明卡、1 个保存按钮。
- 系统页 **74 个元素、36 行状态文字、6 个专家按钮**，却极少访问。
- 环形半径/发光压到“当前内存”标题；三列指标“标签在左、数值极右”造成视觉空洞。

结论：v1.5 是“重排”，不是“减法”。说明卡是在用内容填空白，方向错误。

### 2. 设计原则（本轮依据）

- Microsoft 设置页指南：保持 4–5 个常用设置；单列滚动；高级项用 Expander；
  **修改立即生效，不要确认按钮**。
- Microsoft UX 指南：“Eliminate what can be eliminated”；先删，再谈视觉。
- 仪表盘研究：主指标必须有绝对视觉权重；次要数据放展开区；元素越多认知负荷越高。
- 中文信息层级：单页层级最多 3 层；先分必要/补充/无用，再调视觉。
- Windows 11 Fluent：Calm、Decluttered、Content not chrome。

来源：
- https://learn.microsoft.com/en-us/windows/apps/design/app-settings/guidelines-for-app-settings
- https://learn.microsoft.com/en-us/windows/win32/uxguide/how-to-design-desktop-ux
- https://phenomenonstudio.com/article/why-your-dashboard-looks-busy-and-your-users-feel-lost/
- https://uisdc.com/information-hierarchy
- https://learn.microsoft.com/en-us/windows/apps/design/design-principles

### 3. 新结构：One Screen + Sheet

- 主屏 520×600：状态句 + 细环 + “可用/缓存”一行 + 主按钮 + 自动清理一行 + 上次清理 + 两个文字入口。
- 设置：一张单列 Sheet，无 tabs、无保存按钮；修改即时 save；高级折叠到独立紧凑 Sheet。
- 进程 / 系统：按需 Sheet，按内容缩短高度；系统默认 5 行，更多状态再展开。
- 删除：顶部导航、Page 枚举、页面过渡、4 设置子页、3 统计列、主屏 Top5、状态徽标、
  7 段解释文字、4 张说明卡、6 个系统按钮的默认展示。
- 视觉：主屏无卡片边框；Sheet 是唯一白色表面；12/14/20/44 四档字；单一绿色强调；
  Sheet 200ms ease-out，无页面交叉淡入。

### 4. 保护名单/修剪复核

- Process Lasso SmartTrim：**永不修剪当前前台应用**；只在真实压力下修剪后台大工作集；
  支持排除项；默认关闭。说明“保护前台 + 只动空闲后台”是行业稳健做法。
- `EmptyWorkingSet` 不会降低 Commit：页被移到待机/页面文件；切回应用会重新缺页。
  所以“保护名单全删”看起来释放更多，但一部分只是工作集搬家，且可能增加重载。
- VM（QEMU/WSL）客户机热页被修剪会造成 guest 大范围缺页，收益远小于代价 → 内置硬保护。
- 开发工具（claude/zcode/hermes）不应默认硬保护：前台/父链/120s 焦点历史已保护；
  后台空闲 300s 才自动修剪；手动“立即清理”可修剪后台开发工具。
- 修复 `whitelist=` 空值被 `LoadConfig` 回退成默认名单的问题；空名单现在真正持久化。

来源：
- https://bitsum.com/smarttrim/
- https://randomascii.wordpress.com/2023/10/01/32-mib-working-sets-on-a-64-gib-machine/
- https://stackoverflow.com/questions/1812018/what-are-the-side-effects-of-using-emptyworkingset

### 5. v1.7 复核：自动策略与深度整理

- **Process Lasso SmartTrim**：默认关闭；只修剪大工作集后台，永不碰前台；Purge Standby List
  默认关闭；32GB 建议阈值 75% / 512MB / free-excluding-cache 2048MB。结论：清理器应保守，
  Windows 本身会管理缓存。
- **ISLC**：只有「standby 列表 ≥ 列表阈值」且「free < 空闲阈值」才清；16GB 推荐 1024/2048MB，
  32GB 推荐 2048/4096MB。结论：单看 `free` 会过度清理。
- **Mem Reduct 3.5.2**：默认清理工作集 + 文件缓存 + 低优先级待机 + 合并页；standby/modified
  默认不选，自动清理仍需显式允许。结论：低优先级缓存是常规操作，全量缓存是高风险操作。
- **Windows 11 26H2（26300+）**：微软重构内存压缩/分配器，8GB 机器可多保留最多 2GB 活跃数据，
  后台索引降优先级，Edge 非活动标签 15 分钟休眠。结论：清理器不应频繁全量清缓存，
  否则会抵消 OS 的缓存/压缩收益。
- **页面合并**：Microsoft 文档明确代价是 CPU 占用；本机实测单次合并 560–1100MB，
  耗时 7–52.6s（受负载影响）。结论：只做手动可选，默认关闭，永不自动。
- **本机实测（v1.7）**：快速清理空闲 +3509MB / 可用 +1128MB / 4.14s；
  自动低优先级缓存空闲 +991MB / 0.34s；深度整理 607MB / 9.94s，最慢 52.6s。

来源：
- https://bitsum.com/smarttrim/
- https://bitsum.com/apps/process-lasso/docs/algorithms/smarttrim/
- https://www.makeuseof.com/islc-ram-tool-from-the-ddu-developer-is-surprisingly-useful
- https://github.com/henrypp/memreduct/releases
- https://blogs.windows.com/windowsexperience/2026/11/03/raising-the-bar-for-8-gb-windows-11-pcs-whats-improved-and-how-to-get-it/
- https://learn.microsoft.com/en-us/windows-server/administration/performance-tuning/subsystem/cache-memory-management/improvements-in-windows-server
- https://learn.microsoft.com/en-us/powershell/module/mmagent/enable-mmagent

### 6. 最终状态

- 版本 **v1.7.0.0**，单文件 **356,352 字节**；安装版 SHA256
  `E7E5C446F1741AA4BDD5E648C19DF8D4E93466486D9A2548F4B458517FDEDCF0`。
- 窗口 520×600 逻辑，PerMonitorV2；文档截图按 150% 渲染。
- 主屏 **8.2MB private / 12.7MB WS**；托盘隐藏 **6.5MB**；全新托盘启动 5.9MB。
- 当前硬保护名单为空（用户偏好），VM 仍由内置保护兜底；深度整理默认关闭。
- `build.cmd test` PASS；`/selftest` PASS；零警告。

## 十七、v1.8 并发加固与“离开时深度维护”（2026-11-15 第八轮）

### 1. 代码审计发现并修复的并发问题

- `TakeSnapshot` 的 `static std::vector` 缓冲区被监控/清理/离线快照并发 `resize` 并写入内核数据，
  属于数据竞争（潜在进程列表损坏）→ 改为 `thread_local`。
- `RebuildWhitelist` 无锁 `clear/insert`，与 `IsProtectedProcess` 的锁内读构成 UB →
  锁内构建新 set 后 `swap`。
- UI `CommitSettings` 写 `g_cfg`，后台 `MonitorTick/RunClean` 同时读 → 新增
  `GetConfigSnapshot/PublishConfig`（SRWLOCK 不可变副本），后台全部改走快照。
- `g_hfPerSec(double)/g_last*Gain/g_pauseUntil/g_runtimeIdleSecs/g_zh` 跨线程裸读写 → `std::atomic`。
- `SeDebug/SeProfile/SeIncreaseQuota` 启动常驻 → 改为内核操作前开启、操作后关闭。

### 2. 新技术落地（均为官方/一手来源核验）

- `PROCESS_MEMORY_COUNTERS_EX2.PrivateWorkingSetSize` 与
  `SYSTEM_PROCESS_INFORMATION.WorkingSetPrivateSize`：任务管理器口径；快照直读，不开 500 个句柄。
- `SHQueryUserNotificationState`：只用 `QUNS_RUNNING_D3D_FULL_SCREEN/QUNS_PRESENTATION_MODE`；
  PowerToys 的实践证明 `QUNS_BUSY` 会被 NVIDIA Overlay 误报。
- `CreateMemoryResourceNotification(LowMemoryResourceNotification)`：事件唤醒 + 2s 节流；
  阈值仍用自己的“可用 <512MB / 负载 ≥95%”，因为 OS 通知阈值偏晚（社区资料约每 4GB 32MB）。
- `MEMORY_COMBINE_INFORMATION_EX.EventHandle` + 看门狗：页面合并可中止；
  实测传入事件后 18.1s 正常完成 221,659 页合并，未触发中止。
- `GetLastInputInfo`：会话级空闲；`dwTime` 为 32 位 tick，用无符号差值避免回绕。

### 3. 离开时深度维护实测

- 测试配置 `idleDeepSecs=5`（正式默认 600）：触发条件“空闲≥5s + 使用中 85% + 提交 91%”。
- 执行：空闲候选 0（进程空闲判定未到期，符合保守预期）→ 低优先级缓存 + 页面合并。
- 结果：合并 280,733 页 ≈1.1GB，空闲 39MB→1002MB（+963MB），可用 +751MB，14.9s，正常复位。
- 用户返回路径：watcher 每秒检查 `IsUserIdle(3)`，输入即 `SetEvent` → RunClean 修剪循环与
  CombinePages 同步中止；无净增益的中止不覆盖“上次清理”。

### 4. 最终状态

- 版本 **v1.8.0.0**，单文件 **368,128 字节**；安装版 SHA256
  `478FFF3E8EE74AF3C5C55E9A62DC64E06A818EDC7B9308DC7F25D985E0A09189`。
- `build.cmd test` PASS；离开时维护/深度整理/全量缓存/低优先级缓存策略均有自检覆盖。

来源：
- https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex2
- https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ne-shellapi-query_user_notification_state
- https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-creatememoryresourcenotification
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getlastinputinfo
- https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadpriority
- https://ntdoc.m417z.com/memory_combine_information_ex
- https://github.com/microsoft/PowerToys/pull/45891

## 十八、v1.8.1 文案口径与设置减法（2026-11-15 第九轮）

### 1. 文案基准（来源核验）

- 微软风格指南：先说结果、像说话一样写、剪掉多余词、标签/按钮不带句号、全文句首大写。
- Windows 中文任务管理器口径：使用中 / 可用 / 已缓存 / 已提交 / 分页与非分页缓冲池。
  我们改为：已缓存（standby）、已提交（commit）、内核内存（内核池）、应用预加载（预取/预启动）。
- 同赛道通知格式（WinMemoryCleaner）：优化后通知 = 触发原因 + “freed ~X”。
  我们统一为“已释放约 X，可用 Y”，X 取空闲/可用净增的较大者，杜绝三重数字。

### 2. 减法（高级 8 → 6）

- 抖动保护：安全护栏（硬缺页尖峰时暂停缓存清理），内置永远开启，历史配置 0 自动恢复。
- 深度整理开关：与「清理力度」语义重叠；改为力度=深度即含合并页，老配置 `combinePages=0`
  在加载时自动迁移（`/dryrun` 实测计划 `合并页=1`）。
- 重命名：最小工作集→只清理超过；空闲判定→后台闲置；EcoQoS→后台效率模式（任务管理器同名词）；
  离开时深度维护→离开时自动清理。

### 3. 验证

- 隐藏窗口提权（ProcessStartInfo WindowStyle=Hidden）执行 `/dryrun`：合并页=1，演练不动内存。
- 手动清理：120 进程 / 空闲 +2544MB / 可用 +2994MB / 3.6s；托盘 5.9MB 私有 / 0.2MB WS。
- 最终 v1.8.1.0，364,544 字节，SHA256 `6B421D6577F873033C19310535FA8CA4DD02E3A8EC275FEBF7EFE5E7998965A6`。

来源：
- https://learn.microsoft.com/en-us/style-guide/top-10-tips-style-voice
- https://learn.microsoft.com/en-us/windows/apps/design/style/writing-style
- https://github.com/IgorMundstein/WinMemoryCleaner（通知 = reason + freed ~X）

## 十九、v1.9 动效架构与“可回收估算”（2026-11-15 第十轮）

### 1. “静止像卡死”的根因

缓动逻辑写在了 1s 的数据刷新里（每秒 22% 步进）——5% 的仪表变化要爬 5 秒，
1fps 步进在视觉上就是“卡住→跳一下”。修复：动画与数据彻底分离，
`StepAnimations()` 在 33ms 定时器做帧率无关指数平滑（`1-exp(-dt·k)`）。

### 2. 生命感（轻量约束下）

- 环境高光沿进度弧 7s 一圈（Apple Watch 式），**6.7fps、仅可见主屏、隐藏 0 开销**，
  尊重系统“减少动画”。实测 CPU：可见 3.1% / 隐藏 1.6%（单核 12s 均值）。
- 微交互全套：悬停光晕缓动、Toast 淡入上滑、Sheet 缓动曲线、清理按钮流光（圆角裁剪）。

### 3. 可回收估算（用户批准的唯一新功能）

`ComputeReclaimable()`：空闲候选私有WS + 低优先级缓存（常规）或全量缓存（深度），
监控线程每 tick 复算、离屏渲染复用；主屏“可回收约 X · 可用 Y”。
用户否决了增长检测与 sparkline（拒绝常驻消耗）——已记录为产品约束。

### 4. 最终状态

v1.9.0.0，366,592 字节，SHA256 `D034FF72F4728747C9808C2CA6F1504637C27419EA398B0CD7490523FCB599BA`。
`build.cmd test` PASS 零警告；托盘 5.9MB 私有 / 5.4MB WS。

## 二十、v1.9.2 托盘节流与标题栏快切（2026-11-15 第十一轮）

### 1. 隐藏态 CPU 波动的排查

tick 插桩（`MEMSweep_TICKLOG`，仅环境变量开启）实测：**16ms / 5000ms = 0.32%**——
监控采样本身很便宜。隐藏态 1.6~4.7% 的波动来自整机负载/频率缩放（CPU 时间计法）。
真正的确定性优化是托盘：数值量化后每秒跨阈值 → 图标每秒重建
（DIB+GDI+ 文字 + Shell_NotifyIcon IPC）→ 修复后可见态 3.1%→1.6%。
部分重绘模式依据：MSDN 更新区裁剪（BeginPaint 自动裁剪 + InvalidateRect 局部失效）。

### 2. 首屏快切（用户设计输入落地）

标题栏右侧：`[中/EN]` 文字胶囊 + `[日/月]` 主题图标 + 齿轮 + 最小化 + 关闭。
主题快切为显式浅↔深（跟随系统保留在设置页）；语言切换即时重建字体/布局。
静默模式上移到设置页（动态副文案），高级设置 6→5 项。

### 3. 体积审计（结论记录）

试验并否决：`-falign-*`/`-fmerge-all-constants`（zig cc 下零收益）、
`-fno-(asynchronous-)unwind-tables`（mingw CRT `.seh_` 直接编译失败）、
`-flto`（链接失败）。`-Oz + gc-sections + strip` 为实测最优。

### 4. 最终状态

v1.9.2.0，369,152 字节，SHA256 `70A4654049D9A9633400C9CEAA2B8F716580292947404F22148840BD4CBE52D8`。
`build.cmd test` PASS 零警告；selftest PASS。

来源：
- https://learn.microsoft.com/en-us/windows/win32/gdi/redrawing-in-the-update-region
- https://learn.microsoft.com/en-us/windows/win32/gdi/invalidating-and-validating-the-update-region
- https://signpath.org/terms.html（OSS 签名条件）
- https://learn.microsoft.com/en-us/windows/package-manager/package/repository（WinGet 提交流程）

## 二十一、v1.9.3 交互回归修复（2026-11-16 第十二轮）

### 1. 标题栏按钮失效根因

`WM_NCHITTEST` 的拖拽排除清单只含旧的 齿轮/最小化/关闭——新增的主题/语言按钮
落进 `y < titlebar.Height → HTCAPTION` 分支，点击被系统拖拽吞掉。
修复后自动化点击验证：语言快切即时渲染英文界面 + Toast；主题快切即时应用深色。

### 2. Sheet scrim 透底

scrim alpha 150（59%）下首页状态文字/logo 仍清晰可读——用户感知为“元素混乱”。
加深到 205（78%）。该观感在设置页满高后显眼。

### 3. 测量纪律（教训）

整机内存 96% + 自动清理循环时，进程 CPU 时间被负载/频率缩放污染（1.6~7.8% 波动），
不可作回归信号。工程真相用隔离插桩：`MEMSweep_TICKLOG` 实测 tick 16ms/5000ms = 0.32%。
`ComputeReclaimable` 加可见性门控（UI-only 指标，隐藏时 0 成本）。

### 4. 最终状态

v1.9.3.0，369,664 字节，SHA256 `13066C82734B44149E1A575AD93175A15CB1D34E69DE0D8771069E5CC24B42BD`。
`build.cmd test` PASS 零警告；selftest PASS。

来源：
- https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-nchittest（HTCAPTION/HTCLIENT 语义）
