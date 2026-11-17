# Contributing

MemSweep is maintained by one person. Issues and pull requests are welcome, and small, focused changes are easiest to review.

## Issues

- **Bugs** — include the MemSweep version, `winver`, the last 50 lines of `%APPDATA%\MemSweep\log.txt`, and steps to reproduce. For memory reports, a screenshot of Task Manager → Performance → Memory helps.
- **Features** — describe the problem first. MemSweep follows a subtraction rule: a new setting has to show it cannot fold into an existing one (see invariant 14 in `docs/ENGINEERING.md`).

## Build

```cmd
build.cmd test     :: dev build + /selftest + /idlecheck + /uipng + /checkpng
build.cmd          :: release build (requireAdministrator)
build.cmd arm64    :: Windows on ARM64 cross-compile
```

Requires zig 0.17.1 in `../tools/`, or `ZIG` pointing at `zig.exe`.

## Code rules

- Zero warnings (`-Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Werror`).
- New policy logic must include `/selftest` cases.
- User-facing strings must use Task Manager terms and plain speech, with no internal jargon (invariant 13).
- No synchronous PowerShell, WMI, or `schtasks` calls on the UI thread, and no blocking I/O over 50 ms.
- No third-party runtimes, networking code, or kernel drivers in the application.

## Pull requests

Keep one logical change per PR, and include build and test output. Follow `.editorconfig` and `.clang-format`.

## 中文摘要

本项目由个人维护，欢迎提 Issue 和 PR。报 Bug 请附上版本号、`winver`、`%APPDATA%\MemSweep\log.txt` 最后 50 行和复现步骤；提新功能请先说清具体场景，新增设置项需要证明无法并入现有选项。构建请运行 `build.cmd test`，要求零编译警告，策略改动必须同步补充 `/selftest` 用例。
