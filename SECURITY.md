# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| latest release | ✅ |
| older releases | ❌ |

## Reporting a vulnerability

请使用 GitHub 的 **Private vulnerability reporting**（仓库 Security 标签页 → Report a
vulnerability）私下报告，不要在公开 Issue 中描述细节。会在 7 天内回应。

## 设计边界（并非漏洞，请勿重复报告）

- MemSweep 以管理员权限运行，并调用未公开的 NT 接口
  （`NtSetSystemInformation` 80/81/130 类）清理系统内存列表——这是产品的核心功能，
  与 Sysinternals RAMMap / Mem Reduct 同级，需要管理员授权是设计前提。
- 程序**无网络、无遥测、无注入、无驱动**；所有数据保存在本地
  `%APPDATA%\MemSweep\`。可复核 `src/` 全部源码。
- 前台进程、父进程链、120 秒焦点历史、系统关键进程与保护名单不会被修剪。
