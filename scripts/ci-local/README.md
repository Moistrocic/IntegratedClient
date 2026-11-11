# 本地复刻 GitHub Actions（act）使用说明

目标：在本机尽可能 **原样执行仓库里的 workflow YAML**，减少对 GitHub Actions 配额的消耗。

实现方式：[nektos/act](https://github.com/nektos/act)

- **Linux 作业**：在 Docker 容器中运行，使用 GitHub runner 的社区近似镜像
  `catthehacker/ubuntu:act-*`（预装 actions 所需的 node 与常用工具）。
- **Windows 作业**：`act` 的 host 模式（`-P windows-latest=-self-hosted`），
  直接在本机 Windows 上按 YAML 执行步骤。
- macOS / iOS 作业：**无法本地复刻**（需要 macOS + Xcode），脚本会提示跳过。
- 其他 GitHub 平台能力（secrets、artifact、cache、services、Pages、release）
  由 act 本地服务/参数近似，不能保证与 GitHub 完全一致。

## 前置条件

- Docker Desktop 正在运行（Linux 作业需要）。
- 本机已有：VS2022（C++ 工作负载）、CMake、Ninja、Rust（`%USERPROFILE%\.cargo\bin` 在 PATH）。
- 首次运行需要网络下载：act 二进制、`catthehacker/ubuntu:act-*` 镜像、workflow 中
  `uses:` 的 actions。Docker Desktop 访问 GitHub 偶有超时，失败可重试。

## 用法

```powershell
# 列出某个 workflow 可运行的 job
.\scripts\ci-local\run-ci-local.ps1 -Workflow style -List

# Linux 容器作业（style 全套检查，apt 下载较多，较慢）
.\scripts\ci-local\run-ci-local.ps1 -Workflow style

# Rust 检查（ubuntu-latest 容器）
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -Job check-rust

# Windows 作业：host 模式跑 build.yml 的 windows-latest 矩阵
.\scripts\ci-local\run-ci-local.ps1 -Workflow build -Job build-cmake -Matrix os:windows-latest

# 只跑 Android 作业（Linux 容器，会下载 NDK/SDK，很慢）
.\scripts\ci-local\run-ci-local.ps1 -Workflow build -Job build-android

# 关闭镜像强制拉取 / 复用容器状态
.\scripts\ci-local\run-ci-local.ps1 -Workflow style -NoPull
```

脚本默认：

- `-CpuLimit 8`：同时设置 `--container-options "--cpus=8"`、`CMAKE_BUILD_PARALLEL_LEVEL=8`、
  `CARGO_BUILD_JOBS=8`，遵守 AGENTS.md 的「本地并行度上限 8」。
- 日志写入 `_work/ci/act/logs/`，artifact 写入 `_work/ci/act/artifacts/`。
- 环境变量模板 `_work/ci/act/act.env`、secrets 模板 `_work/ci/act/act.secrets`
  （都在 `_work/` 下，已被 gitignore）。secrets 需要按 workflow 手工填充；
  `VERSION` 等可以为空。
- runner 映射：
  - `ubuntu-latest` → `catthehacker/ubuntu:act-latest`
  - `ubuntu-22.04` → `catthehacker/ubuntu:act-22.04`
  - `ubuntu-24.04` → `catthehacker/ubuntu:act-24.04`
  - `windows-latest` / `windows-2022` → `-self-hosted`（本机 host 模式）
  - `macOS-latest` / iOS 不映射，遇到会直接失败（提示不支持）

## 覆盖能力与限制

| 工作流 | 本地可复刻程度 | 说明 |
| --- | --- | --- |
| `spelling.yml` | ✅ 容器 | `crate-ci/typos` action 首次需联网 |
| `rust.yml` | ✅ 容器 | cargo 构建 |
| `style.yml` | ✅ 容器 | apt 安装较多，Docker Desktop 网络慢时耗时长 |
| `clang-tidy.yml` | ✅ 容器 | 需要下载 clang-20（apt.llvm.org） |
| `clang-sanitizer.yml` | ✅ 容器 | sanitizer 构建与运行 |
| `build.yml` 的 ubuntu / android / emscripten | ✅ 容器 | android/emscripten 需大量下载 |
| `build.yml` 的 windows-latest | ⚠️ host 模式 | 本机是 Windows 11，GitHub 是 Windows Server runner；预装工具不同 |
| `build.yml` 的 windows-latest-mingw | ⚠️ host 模式 | 需要 `gcc/g++/windres` 在 PATH（本机在 `C:\Code\Env\gcc-*`，默认不在） |
| `build.yml` 的 macOS / iOS | ❌ | 需要 macOS + Xcode |
| `fast-build.yml` / `debug-build.yml` | ⚠️ 部分 | Linux 容器可跑；Windows host 可跑；macOS 不可 |
| `release.yml` | ⚠️ 部分 | 依赖 secrets（`BESTCLIENT_OFFICIAL_CODE`、签名证书），只能本地 dry-run |
| `codeql-analysis.yml` / `static.yml` / `update-flake.yml` / `build-libraries-*.yml` | ⚠️/❌ | 依赖 GitHub 服务（CodeQL、Pages、cachix 等），不建议本地复刻 |

**无法 1:1 的部分**：

- GitHub-hosted runner 镜像差异（尤其 Windows Server vs 本机 Windows 11、macOS）。
- `secrets` 内容、`GITHUB_TOKEN` 权限、artifact/cache/Pages/release 等平台服务。
- 并发策略、缓存命中、runner 资源（CPU/内存/超时）与 GitHub 账单/配额。
- `paths-ignore` 等事件过滤：**act 不执行 push 的 path 过滤**，所以即使仓库里
  暂时用 `paths-ignore: '**'` 暂停了 CI，本地 act 仍会按步骤执行。

## 暂停 CI 期间

仓库当前的 `TEMP(quota)` 配置只影响 GitHub 上的触发，不影响本地 act。
配额恢复后 `git revert` 暂停提交即可重新开启 CI，之后可继续用本脚本先本地验证。
