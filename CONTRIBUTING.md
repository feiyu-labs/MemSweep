# Contributing

MemSweep is maintained by one person. Issues and pull requests are welcome, and small, focused changes are easiest to review.

## Issues

**Bug** — include the MemSweep version, `winver`, the last 50 lines of `%APPDATA%\MemSweep\log.txt`, and steps to reproduce. For memory-related reports, a screenshot of Task Manager → Performance → Memory helps.

**Feature** — describe the situation first. MemSweep follows a subtraction rule: a new setting has to show it cannot be folded into an existing one (see invariant 14 in `docs/ENGINEERING.md`).

## Build

```cmd
build.cmd test     :: dev build + /selftest + /idlecheck + /uipng + /checkpng
build.cmd          :: release build (requireAdministrator)
build.cmd arm64    :: Windows on ARM64 cross-compile
```

Requires zig 0.17.1 in `../tools/`, or `ZIG` pointing at `zig.exe`.

## Code rules

- Zero warnings (`-Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith -Werror`).
- New policy logic must come with `/selftest` cases.
- Interface strings use Task Manager terminology; no jargon in user-visible text (invariant 13).
- No synchronous PowerShell, WMI or `schtasks` on the UI thread, and no I/O over 50 ms.
- No third-party runtimes, networking or drivers in the application.

## Pull requests

One change per PR, with build and test output. Follow `.editorconfig` and `.clang-format`.

## 中文摘要

单人维护，欢迎 Issue 与 PR。报 bug 请附版本、`winver`、日志最后 50 行与复现步骤；提功能请先说明使用场景，新设置项需要证明无法并入现有项。构建用 `build.cmd test`，零警告是硬性要求，新策略逻辑必须同步加 `/selftest` 用例。