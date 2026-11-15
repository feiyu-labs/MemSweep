# Contributing

感谢关注 MemSweep。当前是单人维护节奏，接受 Issue 与 PR。

## 提交 Issue

- Bug：附 MemSweep 版本、Windows 版本（winver）、`%APPDATA%\MemSweep\log.txt` 最后 50 行、
  复现步骤。内存问题请附任务管理器“性能→内存”截图（使用中 / 已提交 / 已缓存）。
- 功能：先描述使用场景。本项目遵循“减法”约束——新设置项必须证明无法并入现有项
  （见 `docs/ENGINEERING.md` 不变量 14）。

## 构建

```cmd
build.cmd test     :: dev + /selftest + /idlecheck + /uipng + /checkpng
build.cmd release  :: 正式版（requireAdministrator）
build.cmd arm64    :: Windows on ARM 交叉编译
```

需要 zig 0.17.1（默认路径 `..	ools\`，或设 `ZIG` 环境变量指向 zig.exe）。

## 代码约束

- 零警告（`-Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wpointer-arith`）。
- 新策略逻辑必须同步加入 `/selftest` 用例。
- UI 文案只用任务管理器术语；行话不得出现在用户可见界面（`docs/ENGINEERING.md` 不变量 13）。
- UI 线程禁止同步 PowerShell / WMI / schtasks / >50ms I/O。
- 不引入第三方运行时、网络、驱动；不改这些会直接 close。

## 提交 PR

- 一个 PR 一件事，附带构建/测试结果。
- 遵守 `.editorconfig` / `.clang-format`。
