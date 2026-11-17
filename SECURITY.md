# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| Latest release | ✅ |
| Older releases | ❌ |

## Reporting a vulnerability

Use GitHub **private vulnerability reporting** (repository → Security → Report a vulnerability). Please do not disclose details in a public issue. You can expect a response within seven days.

## Expected behavior (not vulnerabilities)

These are core design decisions rather than security defects:

- **Administrator privileges and NT memory calls.** Purging standby lists and trimming the file cache require administrator rights and `NtSetSystemInformation` (classes 80, 81, and 130), similar to Sysinternals RAMMap.
- **No networking, telemetry, injection, or drivers.** All state stays in `%APPDATA%\MemSweep\` (or beside the binary in portable mode), and all source files live in `src/`.
- **Foreground and critical processes stay protected.** The active window, its parent chain, recent focus history, critical Windows processes, and your protected list are never trimmed.

## 中文摘要

安全问题请通过 GitHub 的私有漏洞报告通道提交（Security → Report a vulnerability），不要在公开 Issue 里披露细节，七天内会回复。以管理员权限运行并调用 NT 内存清理接口属于产品本身功能，并非漏洞；程序无网络、无遥测、无注入、无驱动，全部源码可在 `src/` 中核对。
