# MemSweep

<p align="center">
  <a href="README.md"><b>English</b></a> · <a href="README.zh-CN.md">简体中文</a>
</p>

<p align="center">
  <img src="docs/logo.png" alt="MemSweep" width="96" height="96" />
</p>

<p align="center">
  <strong>Reclaim memory without interrupting your work.</strong>
</p>

<p align="center">
  <a href="https://github.com/feiyu-labs/MemSweep/actions/workflows/ci.yml"><img src="https://github.com/feiyu-labs/MemSweep/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/feiyu-labs/MemSweep/releases/latest"><img src="https://img.shields.io/github/v/release/feiyu-labs/MemSweep?color=emerald" alt="Release"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue.svg" alt="License"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20(x64%20%2C%20ARM64)-blue" alt="Platform">
  <img src="https://img.shields.io/badge/binary-403%20KB-success" alt="Binary size">
  <img src="https://img.shields.io/badge/tray-3%20MB-success" alt="Tray memory">
  <img src="https://img.shields.io/badge/network-none-green" alt="No network access">
</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#features">Features</a> ·
  <a href="#command-line">Command line</a> ·
  <a href="#footprint">Footprint</a> ·
  <a href="#build-from-source">Build</a> ·
  <a href="CHANGELOG.md">Changelog</a>
</p>

<table align="center">
  <tr>
    <th align="center">Light</th>
    <th align="center">Dark</th>
  </tr>
  <tr>
    <td><img src="docs/screenshot-home-light-en.png" alt="MemSweep, light theme" width="380"></td>
    <td><img src="docs/screenshot-home-dark-en.png" alt="MemSweep, dark theme" width="380"></td>
  </tr>
</table>

## What it is

MemSweep reclaims memory held by idle background processes when Windows is under real pressure, so the applications you are working in keep their speed.

It leaves out what makes other cleaners unreliable: no modified-page flushes, no global working-set sweeps, no process termination, no background services, no network access.

| | |
|:---|:---|
| **Platform** | Windows 10 and 11, native x64 and ARM64 builds |
| **Size** | 403 KB binary, no runtime dependencies |
| **Memory** | 9.5 MB open · 3.0 MB in the tray |
| **License** | MIT |

## Features

- **Reclaims from the background, never the foreground.** The focused window, its parent process chain, anything focused in the last two minutes, and system or virtualisation services are excluded from every pass. A background process joins the candidate list after five minutes of near-idle CPU.
- **Decides on pressure, not on a percentage.** A pass starts when your threshold is crossed *and* the system is short: under 512 MB available, or 95% in use. Memory parked on the standby list is normal Windows behaviour, not a signal to clean.
- **Skips every operation that causes freezes.** No modified-page flush, no registry-cache flush, no global `MemoryEmptyWorkingSets`, no timer-resolution changes, no terminating processes.
- **Merges duplicate physical pages on request**, with a kernel handle that aborts on your first keypress.
- **Never interrupts.** No notifications while a fullscreen game, a presentation or Focus Assist is active; automatic passes report only when they actually free memory.
- **One window, no dependencies.** 403 KB, 3.0 MB in the tray, two themes, two languages, a hotkey you can remap.

<table align="center">
  <tr>
    <td align="center"><b>Settings</b></td>
    <td align="center"><b>Processes and protection</b></td>
  </tr>
  <tr>
    <td><img src="docs/screenshot-settings-en.png" alt="Settings sheet" width="380"></td>
    <td><img src="docs/screenshot-processes-en.png" alt="Processes sheet" width="380"></td>
  </tr>
  <tr>
    <td align="center"><b>System status</b></td>
    <td align="center"><b>Set the hotkey</b></td>
  </tr>
  <tr>
    <td><img src="docs/screenshot-system-en.png" alt="System status sheet" width="380"></td>
    <td><img src="docs/screenshot-hotkey-en.png" alt="Recording a new hotkey" width="380"></td>
  </tr>
</table>

## Install

Download `MemSweep-v1.9.14-x64.zip` or `MemSweep-v1.9.14-arm64.zip` from [Releases](https://github.com/feiyu-labs/MemSweep/releases/latest), extract it, then right-click `install.cmd` and choose **Run as administrator**.

The script copies `MemSweep.exe` to `C:\Program Files\MemSweep\`, registers a logon task with elevated privileges so startup never prompts, adds a desktop shortcut and starts MemSweep in the tray.

To remove it, run `uninstall.cmd` as administrator. Settings and logs stay in `%APPDATA%\MemSweep\`.

MemSweep also runs portably: start `MemSweep.exe` from any folder, configuration stays in `%APPDATA%\MemSweep\config.ini`.

## Command line

```cmd
MemSweep.exe /clean          :: clean once, then exit
MemSweep.exe /dryrun         :: report what would be cleaned; change nothing
MemSweep.exe /combine        :: merge duplicate physical pages
MemSweep.exe /trimpid <PID>  :: trim one process
MemSweep.exe /pause          :: pause automatic cleaning for one hour
MemSweep.exe /resume         :: resume automatic cleaning
MemSweep.exe /tray           :: start in the tray (used by the logon task)
MemSweep.exe /show           :: open the window, or forward to the running instance
MemSweep.exe /selftest       :: run the policy engine self-test; exit code 0 on pass
MemSweep.exe /exit           :: shut down the running instance
```

## Footprint

Windows 11 26H2 (26340), i7-12800HX, 24 GB RAM, 150% scaling. Figures are the private working set Task Manager reports.

| State | Private working set | Private commit |
|:---|:---:|:---:|
| Window open | 9.5 MB | 12.2 MB |
| Minimised to tray | 6.0 MB | 9.2 MB |
| Tray, settled | **3.0 MB** | 9.6 MB |

A manual clean on this machine has reclaimed 1–3 GB of free pages; the amount depends on what is running at the time.

## Compared with

| | MemSweep | Mem Reduct | WinMemoryCleaner | Process Lasso (SmartTrim) | ISLC |
|:---|:---:|:---:|:---:|:---:|:---:|
| Binary size | 403 KB | ~350 KB | ~10 MB (.NET) | ~80 MB (suite) | ~1.5 MB |
| Tray memory | 3.0 MB | ~4.5 MB | ~35 MB | ~45 MB | ~18 MB |
| Foreground immunity | Window, parent chain, 120 s focus history | No | No | Exclusion list | No |
| Pressure-based trigger | Yes | No | No | Partial | Partial |
| Modified-page flush | Never | Yes | Yes | No | No |
| Page combining | Yes, abortable | No | No | No | No |
| ARM64 build | Yes | No | No | Yes | No |

Rows describe published defaults and documented behaviour as of November 2026. Competitors change; check their documentation before relying on a row.

## Build from source

MemSweep builds with [Zig](https://ziglang.org/) as the C++ toolchain, so no Visual Studio installation is required.

```cmd
git clone https://github.com/feiyu-labs/MemSweep.git
cd MemSweep

build.cmd          :: release build  (dist\MemSweep.exe)
build.cmd test     :: dev build, self-test, idle check, headless UI renders
build.cmd arm64    :: Windows on ARM64
```

The pinned compiler is zig 0.17.1: place it in `../tools/` next to the repository, or point `ZIG` at `zig.exe`. Warnings are errors, and `build.cmd test` is what CI runs.

## FAQ

**The percentage in Task Manager did not drop after cleaning.**
That figure counts in-use physical pages. When background processes are protected or recently active, MemSweep reclaims standby cache instead, which shortens the next allocation rather than moving the percentage.

**Can it lower committed memory?**
No. Commit is virtual memory promised to applications; trimming moves pages between RAM, standby and the pagefile. To lower it, close applications or reduce heap limits (`-Xmx`, `--max-old-space-size`).

**Why does it need administrator rights?**
Purging standby lists and trimming the file cache are privileged NT operations. The installer registers a logon task, so daily use never prompts.

## Privacy

No network code: no telemetry, no update checks, no data leaves the machine. The only outbound links open a browser when you click them. Configuration and logs stay in `%APPDATA%\MemSweep\`.

## Community

Bug reports and pull requests are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md); report security issues privately as described in [SECURITY.md](SECURITY.md). Design decisions are in [docs/ENGINEERING.md](docs/ENGINEERING.md), and the research behind the thresholds in [RESEARCH.md](RESEARCH.md).

## License

[MIT](LICENSE) © feiyu-labs.