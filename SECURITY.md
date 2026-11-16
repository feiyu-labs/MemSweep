# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| latest release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

Use GitHub **private vulnerability reporting** (repository → Security → Report a vulnerability). Do not describe details in a public issue. Expect a reply within seven days.

## Not a vulnerability

These are design decisions, not defects:

- **Running elevated and calling NT interfaces.** Purging system memory lists requires `NtSetSystemInformation` classes 80/81/130 and administrator rights. It is the product's core function, in the same territory as Sysinternals RAMMap and Mem Reduct.
- **No network, telemetry, injection or driver.** All state lives in `%APPDATA%\MemSweep\`, and the entire source is in `src/`.
- **Protected processes are left alone.** The focused window, its parent chain, recent focus history, critical system processes and the protected list are never trimmed.

## 中文摘要

漏洞请用 GitHub 的私有漏洞报告通道提交，不要在公开 Issue 里写细节，七天内回复。以管理员权限运行并调用 NT 内存接口是产品设计本身，不是漏洞；程序无网络、无遥测、无注入、无驱动，源码全部可复核。