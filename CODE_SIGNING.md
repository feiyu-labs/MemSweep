# Code Signing Policy

**Status: in preparation.** MemSweep plans to apply for free open-source code signing through [SignPath Foundation](https://signpath.org). Current releases are unsigned; verify downloads against the published `.sha256` files.

## Planned setup

- Free code signing is provided by SignPath.io using a certificate from the SignPath Foundation.
- Only artifacts built by this repository's GitHub Actions workflow are signed; local builds are never signed.
- Every signed release requires manual approval from the repository owner (SignPath Approver role).
- Privacy notice: MemSweep does not transfer any information to external systems unless explicitly requested by the user. The application contains no network code, which you can verify in `src/` and the workflow files.

## 中文摘要

代码签名正在筹备中。MemSweep 计划通过 SignPath Foundation 为开源项目申请免费代码签名；当前发布包尚未签名，请使用发布页提供的 `.sha256` 文件核对校验值。正式接入后，只有 GitHub Actions 构建的产物会在维护者手动批准后签名，本地构建不签名。本程序不含任何网络代码，不会向外发送数据。
