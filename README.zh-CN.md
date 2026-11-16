# MemSweep

<p align="center">
  <a href="README.md">English</a> · <a href="README.zh-CN.md"><b>简体中文</b></a>
</p>

<p align="center">
  <img src="docs/logo.png" alt="MemSweep" width="96" height="96" />
</p>

<p align="center">
  <strong>只在内存真正不够用时才动手的 Windows 内存管理工具。</strong>
</p>

<p align="center">
  <a href="https://github.com/feiyu-labs/MemSweep/actions/workflows/ci.yml"><img src="https://github.com/feiyu-labs/MemSweep/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/feiyu-labs/MemSweep/releases/latest"><img src="https://img.shields.io/github/v/release/feiyu-labs/MemSweep?color=emerald" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20(x64%20%2C%20ARM64)-blue" alt="Platform">
  <img src="https://img.shields.io/badge/binary-403%20KB-success" alt="Binary size">
  <img src="https://img.shields.io/badge/memory-3%20MB%20in%20tray-success" alt="Tray memory">
  <img src="https://img.shields.io/badge/network-none-green" alt="No network access">
</p>

<p align="center">
  <a href="#安装">安装</a> ·
  <a href="#工作原理">工作原理</a> ·
  <a href="#命令行">命令行</a> ·
  <a href="#从源码构建">构建</a> ·
  <a href="CHANGELOG.md">更新日志</a> ·
  <a href="SECURITY.md">安全</a>
</p>

---

## 概览

MemSweep 面向的是常年贴着内存上限工作的 Windows 工作站：IDE、浏览器、Electron 应用、容器和虚拟机，每一个都占着几个 GB。

它只做一件事——当系统真的缺内存时，把后台闲置进程占着却不用的页面收回来，让前台能继续干活。工具本身很小，能做的事也刻意收得很窄，并且尽量做到无感：不装驱动、不起服务、不联网，也不替你改任何你没要求过的系统设置。

| | |
|:---|:---|
| **作用范围** | 后台闲置进程占用的页面、已缓存、重复的物理页 |
| **运行平台** | Windows 10 / 11，x64 与 ARM64 各自原生构建 |
| **占用** | 二进制 403 KB · 面板打开时专用工作集 9.5 MB · 托盘稳定后 3.0 MB |
| **依赖** | 无。不需要 .NET、WebView2 或 VC++ 运行库 |
| **许可** | MIT |

<table align="center">
  <tr>
    <th align="center">浅色</th>
    <th align="center">深色</th>
  </tr>
  <tr>
    <td><img src="docs/screenshot-home-light-zh.png" alt="MemSweep 浅色主题" width="380"></td>
    <td><img src="docs/screenshot-home-dark-zh.png" alt="MemSweep 深色主题" width="380"></td>
  </tr>
</table>

---

## 工作原理

### 等真正的内存不足再动手

自动清理的触发条件有两个：内存占用高于你设定的阈值，**并且**系统确实快用完了——可用低于 512 MB，或已用超过 95%。Windows 把空闲内存用作已缓存是正常机制，不是内存不足，MemSweep 不会把它当成信号。这一条区分的正是"能帮上忙的工具"和"会自己循环的工具"。

自动清理真的回收到了东西（64 MB 以上）时，托盘会告诉你一次——十分钟内最多一次，面板开着时不打扰，静默模式下完全不发声。什么也没找到的那几次一个字都不说；按钮下面那行会写明上一次是自动清理还是你自己点的。

通知还有一道总闸：只要 Windows 报告用户当前不接受通知（全屏游戏、演示模式、专注助手），就整条不发；前台窗口铺满整个显示器（排除桌面与任务栏）时同样安静，这条专门兜住那些仍被系统判为"接受通知"的无边框全屏游戏。离开期间的提示不会排队补发，信息都在托盘悬浮提示和面板那行里；自动清理的通知是静音的。

### 正在用的东西一律不碰

当前焦点窗口、它的完整父进程链、两分钟内获得过焦点的应用，以及关键系统服务与虚拟机，都不会被修剪；最近五分钟内有活动的进程同样排除在外。候选只有工作集超过 128 MB、CPU 近乎零增长的后台进程。同一个进程十分钟内不会重复修剪，除非它的工作集又涨了五成以上。

### 明确不做那些得不偿失的操作

有些操作纸面上能"释放内存"，实际代价远大于收益。MemSweep 不做：

| 操作 | 后果 |
|:---|:---|
| 刷写修改页（`MemoryFlushModifiedList`） | 强制写盘、全机卡住数秒，回收的可用内存却接近于零——"清理软件让电脑卡死"基本都是这一步造成的 |
| 刷写注册表缓存 | 只有磁盘 I/O，没有可复用的页面 |
| 全局 `MemoryEmptyWorkingSets` | 把你正在敲字的窗口一起修剪掉，随后它要把每个页面重新读回来 |
| 调高系统定时器分辨率 | 破坏电源管理、缩短续航，换不来可测量的收益 |
| 结束或挂起进程 | 不属于内存管理的范围 |

### 可以合并重复的物理内存

Windows 支持把内容完全相同的物理页合并为写时复制引用。MemSweep 把它做成一个显式动作，并挂上内核事件句柄：只要你碰一下键盘或鼠标，合并会在毫秒级中止。它默认关闭，也永远不会进入自动流程，因为完整扫描需要 10–60 秒。

<table align="center">
  <tr>
    <td align="center"><b>设置</b></td>
    <td align="center"><b>进程与保护</b></td>
  </tr>
  <tr>
    <td><img src="docs/screenshot-settings-zh.png" alt="设置面板" width="380"></td>
    <td><img src="docs/screenshot-processes-zh.png" alt="进程面板" width="380"></td>
  </tr>
  <tr>
    <td align="center"><b>系统状态</b></td>
    <td align="center"><b>高级设置</b></td>
  </tr>
  <tr>
    <td><img src="docs/screenshot-system-zh.png" alt="系统状态面板" width="380"></td>
    <td><img src="docs/screenshot-advanced-zh.png" alt="高级设置面板" width="380"></td>
  </tr>
</table>

界面是一个 520×600 的单窗口。设置、进程和系统状态以浮层盖在它上面，而不是另起页面；底部两张卡片直接显示当前最占内存的应用和内存子系统的状态。

MemSweep 还会读取内存压缩、页面合并、SysMain、分页文件与已提交的情况，前两项如果被别的优化脚本关掉了，可以一键恢复。

### 修改全局热键

清理热键可以自己换。打开**设置 → 高级设置 → 全局热键**，点击右侧的组合键，按下想要的组合即可。只按住修饰键时会实时预览 `Ctrl+Alt…` 而不提交，`Esc` 保留原来的组合，点到窗口外面自动放弃。

组合键交给 Windows 验证，不靠猜：缺少修饰键、属于保留组合（`Alt+Tab`、`Alt+F4`、MemSweep 自己的 `Ctrl+S`）、以及被别的程序占用，这三种情况会分别显示在行下方，录制状态保持不变，可以直接换一个再按。新组合注册失败时旧组合继续生效——只有注册成功才会写入配置。

<p align="center">
  <img src="docs/screenshot-hotkey-zh.png" alt="录制新的全局热键" width="380">
</p>

---

## 安装

从 [Releases](https://github.com/feiyu-labs/MemSweep/releases/latest) 下载 `MemSweep-v1.9.14-x64.zip` 或 `MemSweep-v1.9.14-arm64.zip`，解压后右键 `install.cmd`，选择**以管理员身份运行**。

脚本会把 `MemSweep.exe` 复制到 `C:\Program Files\MemSweep\`，注册一个最高权限的登录计划任务（因此开机不会弹 UAC），创建桌面快捷方式，并让 MemSweep 进入托盘。

卸载时右键 `uninstall.cmd` 以管理员身份运行即可。配置和日志保留在 `%APPDATA%\MemSweep\`，删除该目录后不留任何残留。

MemSweep 也可以绿色运行：从任意目录启动 `MemSweep.exe`，配置保存在 `%APPDATA%\MemSweep\config.ini`。

> [!NOTE]
> 清理系统待机列表和系统文件缓存属于受保护操作，需要 `SeProfileSingleProcessPrivilege` 与 `SeIncreaseQuotaPrivilege` 特权。安装脚本注册计划任务，就是为了不必每次使用都申请提权。

---

## 命令行

```cmd
MemSweep.exe /clean          :: 清理一次后退出
MemSweep.exe /dryrun         :: 只报告将要清理什么，不改动内存
MemSweep.exe /combine        :: 合并重复的物理页
MemSweep.exe /trimpid <PID>  :: 修剪单个进程
MemSweep.exe /pause          :: 暂停自动清理一小时
MemSweep.exe /resume         :: 恢复自动清理
MemSweep.exe /tray           :: 静默进入托盘（登录任务使用）
MemSweep.exe /show           :: 打开窗口，或转发给正在运行的实例
MemSweep.exe /selftest       :: 运行策略引擎自检，通过返回 0
MemSweep.exe /exit           :: 退出正在运行的实例
```

---

## 实测占用

测试环境：Windows 11 26H2（26340）、i7-12800HX、24 GB 内存、150% 显示缩放。内存数字为任务管理器中的专用工作集。

| 状态 | 专用工作集 | 工作集 | 私有提交 |
|:---|:---:|:---:|:---:|
| 面板打开 | 9.5 MB | 17.0 MB | 12.2 MB |
| 最小化到托盘 | 6.0 MB | 8.4 MB | 9.2 MB |
| 托盘稳定 | **3.0 MB** | 3.6 MB | 9.6 MB |

窗口隐藏时会销毁后台缓冲（该缩放下为 780×900×4 字节），前两行的差距主要来自这里。各清理路径的典型结果：

| 操作 | 结果 |
|:---|:---|
| 手动清理 | 空闲 +3,509 MB，可用 +1,128 MB，用时 4.1 秒 |
| 低优先级缓存释放 | 空闲 +991 MB，用时 0.34 秒 |
| 页面合并 | 合并 155,320 页，约 607 MB |
| 离开时维护 | 合并 280,733 页，约 1.1 GB，有输入即中止 |

这些数字完全取决于当时在跑什么程序，列出来只是说明各条路径的量级，不是承诺。

---

## 与其他工具的区别

同类工具大多更早、更重，或者解决的是另一个问题。日常使用中真正有差别的几点：

| | MemSweep | Mem Reduct | WinMemoryCleaner | Process Lasso (SmartTrim) | ISLC |
|:---|:---:|:---:|:---:|:---:|:---:|
| 体积 | 403 KB | ~350 KB | ~10 MB（.NET） | ~80 MB（套件） | ~1.5 MB |
| 托盘占用 | 3.0 MB | ~4.5 MB | ~35 MB | ~45 MB | ~18 MB |
| 前台免疫 | 窗口、父进程链、120 秒焦点历史 | 无 | 无 | 排除名单 | 无 |
| 触发判定真实枯竭 | 是 | 否 | 否 | 部分 | 部分 |
| 刷写修改页 | 从不 | 会 | 会 | 不会 | 不会 |
| 物理页合并 | 有，可中止 | 无 | 无 | 无 | 无 |
| ARM64 构建 | 有 | 无 | 无 | 有 | 无 |

表中描述的是各工具截至 2026 年 11 月的默认设置与已记录行为；同类工具会更新，引用前请核对它们的文档。如果某个你觉得更好的工具没出现在表里，那是遗漏，不是结论。

---

## 从源码构建

MemSweep 使用 [Zig](https://ziglang.org/) 作为 C++ 工具链，不需要安装 Visual Studio。

```cmd
git clone https://github.com/feiyu-labs/MemSweep.git
cd MemSweep

build.cmd          :: 正式版（dist\MemSweep.exe）
build.cmd test     :: 开发版、自检、空闲检查、无头界面渲染
build.cmd arm64    :: Windows on ARM64
```

编译器固定为 zig 0.17.1，放在仓库同级 `../tools/`，或用 `ZIG` 环境变量指向 `zig.exe`。编译开启 `-Werror`，`build.cmd test` 就是 CI 执行的那套检查。

---

## 常见问题

**清理之后任务管理器里的百分比没降。**
那个百分比是「使用中」物理页。如果后台进程都在前台、受保护或刚刚活跃过，MemSweep 会刻意不动它们；此时清理把已缓存转入空闲列表，降低的是后续分配的延迟，而不是百分比。面板汇报的是空闲与可用的增量，不会换成一个更好看的数字。

**能降低已提交内存吗？**
不能，任何工具都不能。已提交是分配给应用的虚拟内存承诺，修剪工作集只是在物理内存、已缓存和分页文件之间搬运页面。要降低已提交，只能关闭应用或收紧堆上限（`-Xmx`、`--max-old-space-size`）。

**为什么体积这么小？**
三个 C++ 编译单元，使用 `-Oz`、`-ffunction-sections`、`--gc-sections` 并剥离符号，只链接 Windows 系统库。

**Windows 10 能用吗？**
可以。开发和测试在 Windows 11 上进行，构建目标是 Windows 10 及以上。

---

## 隐私

MemSweep 没有网络代码：不收集遥测、不检查更新、不向任何地方发送数据；应用里唯一的对外链接需要你点击才会打开浏览器。配置和日志都留在 `%APPDATA%\MemSweep\`。

---

## 参与

欢迎提交问题与改进。动手做功能前请先读 [CONTRIBUTING.md](CONTRIBUTING.md)；安全问题的私下报告方式见 [SECURITY.md](SECURITY.md)。设计取舍与各项安全规则的来由记录在 [docs/ENGINEERING.md](docs/ENGINEERING.md)，阈值背后的调研记录在 [RESEARCH.md](RESEARCH.md)。

## 许可

[MIT](LICENSE) © feiyu-labs。
