# Code Signing Policy

**状态：筹备中。** MemSweep 计划通过 [SignPath Foundation](https://signpath.org)
获取面向开源项目的免费代码签名（当前 Release 为未签名构建，SHA256 见 Release 说明）。

计划内容：

- 免费签名由 SignPath.io 提供，证书由 SignPath Foundation 签发。
- 只签名由本仓库 CI（GitHub Actions）构建的产物；任何本地构建不签名。
- 发布签名需维护者手动批准（SignPath Approver 角色 = 仓库 Owner）。
- 隐私声明：本程序不会向任何网络系统传输数据，除非用户主动请求
  （程序本身无网络代码，可用 `src/` 源码与构建脚本复核）。

提交申请前需满足 SignPath 条件：公开仓库、OSI 许可证（MIT）、已有发布、
双因素认证、自动化构建——本项目按此准备。
