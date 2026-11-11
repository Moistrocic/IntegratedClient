# 本地复刻 GitHub Actions（act）使用说明

目标：在本机尽可能 **原样执行仓库里的 workflow YAML**，减少对 GitHub Actions 配额的消耗。

实现方式：[nektos/act](https://github.com/nektos/act) v0.2.97。

- **Linux 作业**：在 Docker 容器中运行，使用 GitHub runner 的社区近似镜像
  `catthehacker/ubuntu:act-*`。本仓库另外提供 `ic-ci-runner:*` 扩展镜像
  （`scripts/ci-local/runner/Dockerfile`），在 act 镜像上补齐 GitHub Ubuntu runner
  预装的 Rust/CMake/Ninja/clang/pip，否则 workflow 里的 `cargo` 等命令会 `command not found`。
- **Windows 作业**：`act` 的 host 模式（`-P windows-latest=-self-hosted`），直接在本机
  Windows 上按 YAML 执行步骤。act 会把仓库复制到 `%USERPROFILE%\.cache\act\<16hex>\hostexecutor`
  作为 `GITHUB_WORKSPACE`，并用本地工作树副本代替 `actions/checkout`
  （所以本地未提交改动也会参与验证；`release/` 等构建产物不污染仓库）。
- **macOS / iOS 作业**：**无法本地复刻**（需要 macOS + Xcode），脚本不映射，遇到会直接失败。
- act **不执行** `paths-ignore` / `branches-ignore`：即使仓库用 `paths-ignore: '**'`
  暂停了 push 触发，本地仍会按真实 workflow 步骤执行（这正是暂停 CI 期间需要的）。

**当前实测状态（2026-11-11）**：

- Linux 容器作业：`rust.yml` 的 `rustfmt` 作业（扩展 runner 镜像）**PASS**。
- Windows host 作业：`fast-build.yml` 的 `build-cmake -Matrix os:windows-latest` **PASS**
  （Vulkan SDK → Release 构建 → 打包 → upload-artifact 全流程）。
- 结论与日志见 `_work/ci-local/report.md`；以下「已验证覆盖」列出实测范围。

## 前置条件

- Docker Desktop 正在运行（Linux 作业需要；Windows host 作业不强制，但建议开启）。
- 本机已有：VS2022（C++ 工作负载）、CMake、Ninja、Rust（`%USERPROFILE%\.cargo\bin` 在 PATH）。
- 首次运行需要网络下载：act 二进制、`catthehacker/ubuntu:act-*` 镜像、workflow 中 `uses:` 的 actions。
  Docker Desktop 访问 GitHub 偶有超时，失败可重试（脚本已内置一次网络失败自动重试）。
- **并行度上限 8（AGENTS.md）**：脚本默认 `-CpuLimit 8`、`-ConcurrentJobs 1`，容器加
  `--cpus=8`，并写入 `CMAKE_BUILD_PARALLEL_LEVEL=8` / `CARGO_BUILD_JOBS=8` /
  `CTEST_PARALLEL_LEVEL=8`；不要同时跑多个重型 act 任务。

## 用法

```powershell
# 列出某个 workflow 可运行的 job
.\scripts\ci-local\run-ci-local.ps1 -Workflow style -List

# Rust 格式门禁（Linux 容器 + 扩展 runner 镜像，实测 PASS，约 4-5 分钟）
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -Job rustfmt -UseCustomRunner

# style 全套检查（apt 下载较多，较慢，未实测）
.\scripts\ci-local\run-ci-local.ps1 -Workflow style

# Windows 作业：host 模式跑 fast-build.yml 的 windows-latest 矩阵（实测 PASS，缓存命中后约 6-7 分钟）
.\scripts\ci-local\run-ci-local.ps1 -Workflow fast-build -Job build-cmake -Matrix os:windows-latest

# 只跑 Linux 上更快/更省：把仓库 bind 进容器（不再复制 4GB+，但步骤会写本地工作树）
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -Job rustfmt -UseCustomRunner -Bind

# 强制重新拉取 runner 镜像（默认 --pull=false，复用本地镜像）
.\scripts\ci-local\run-ci-local.ps1 -Workflow style -ForcePull
```

> 脚本默认 `--pull=false`（本地已有镜像就不拉取），只有 `-ForcePull` 才强制拉取镜像。

脚本默认行为：

- 日志写入 `_work/ci/act/logs/`，artifact 写入 `_work/ci/act/artifacts/`；
- 环境变量模板 `_work/ci/act/act.env`、secrets 模板 `_work/ci/act/act.secrets`
  （都在 `_work/` 下，已被 gitignore）。secrets 需要按 workflow 手工填充，`VERSION` 等可以为空；
- runner 映射：
  - `ubuntu-latest` → `ic-ci-runner:latest`（`-UseCustomRunner`）或 `catthehacker/ubuntu:act-latest`
  - `ubuntu-22.04` → `ic-ci-runner:22.04` / `catthehacker/ubuntu:act-22.04`
  - `ubuntu-24.04` → `ic-ci-runner:24.04` / `catthehacker/ubuntu:act-24.04`
  - `windows-latest` / `windows-2022` → `-self-hosted`（本机 host 模式）
  - macOS / iOS 不映射，遇到直接失败
- `-UseCustomRunner` 时若某个 `ic-ci-runner:<tag>` 不存在，会警告并回退到对应的
  `catthehacker/ubuntu:act-*`（不会再因为本地没有 22.04 扩展镜像就 pull 失败）。

### 参数

| 参数 | 默认 | 说明 |
| --- | --- | --- |
| `-Workflow` | 必填 | workflow 名（`style`/`build`/`rust`/`fast-build`…）或 YAML 路径 |
| `-Job` | 空 | 只跑一个 job id（act `-j`） |
| `-Event` | `push` | `push` / `pull_request` / `merge_group` |
| `-Matrix` | 空 | 矩阵过滤，如 `-Matrix os:windows-latest`（可重复） |
| `-CpuLimit` | `8` | 容器 `--cpus`、`CMAKE_BUILD_PARALLEL_LEVEL` 等上限 |
| `-ConcurrentJobs` | `1` | 单次 act 内并发作业数。act 默认按 CPU 数并发，会叠满多路 8 线程构建 |
| `-List` | 关 | 只列出 job（act `--list`） |
| `-UseCustomRunner` | 关 | 使用本地扩展 runner 镜像 `ic-ci-runner:*` |
| `-BuildRunnerImages` | 关 | 先构建扩展 runner 镜像 |
| `-RunnerImages` | `latest` | 构建哪些扩展镜像：`latest` / `22.04` / `24.04` |
| `-ForcePull` | 关 | 强制拉取 runner 镜像（默认复用本地镜像） |
| `-RefreshActions` | 关 | 重新拉取 `uses:` 动作（默认 `--action-offline-mode`，命中缓存不联网） |
| `-MaxAttempts` | `2` | 网络拉取 action 失败 / artifact 端口被占用时的最大尝试次数 |
| `-Bind` | 关 | act `--bind`：仓库挂进 Linux 容器，跳过复制（更快，但步骤会写工作树） |
| `-KeepHostWorkspaces` | 关 | 保留 act host 模式的 `%USERPROFILE%\.cache\act\<hex>\hostexecutor` 残留 |
| `-Reuse` | 关 | act `-r`，保留容器 |
| `-ActBin` | `_work/tools/act/bin/act.exe` | 覆盖 act 可执行文件 |

### 构建/更新扩展 runner 镜像

```powershell
# 只构建 ubuntu-latest（默认）
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -BuildRunnerImages

# 同时构建 22.04 / 24.04
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -BuildRunnerImages -RunnerImages latest,22.04,24.04 -List

# 手工等价命令
docker build -f scripts/ci-local/runner/Dockerfile `
  --build-arg BASE_IMAGE=catthehacker/ubuntu:act-latest `
  -t ic-ci-runner:latest scripts/ci-local/runner
```

基础镜像更新后需要重建（`-BuildRunnerImages` 会重新 `docker build`）。

## 已验证覆盖

| workflow / job | 本地复刻 | 实测（2026-11-11） |
| --- | --- | --- |
| `rust.yml` `rustfmt`（ubuntu-latest） | ✅ 容器 + 扩展镜像 | **PASS**（真实 YAML：checkout→rust-cache→`cargo fmt -- --check`） |
| `fast-build.yml` `build-cmake` `os:windows-latest` | ✅ host 模式 | **PASS**（Vulkan SDK 1.3.296、Release 构建、package_default 打包、upload-artifact） |
| `rust.yml` `rustdoc` / `cxxbridge` | ✅ 容器 | 理论可跑；`cxxbridge` 要 `cargo install cxxbridge-cmd` 联网，未逐条实测 |
| `rust.yml` `cargo-deny` | ⚠️ 容器 | `EmbarkStudios/cargo-deny-action` 需联网拉取/`cargo-deny` 下载，未实测 |
| `style.yml` `check-style` | ✅ 容器（慢） | 需 apt 安装 + wget clang-format-20/doxygen/shfmt + `cargo install twmap-tools`，未实测 |
| `spelling.yml` | ✅ 容器 | `crate-ci/typos` 首次需联网，未实测 |
| `clang-tidy.yml` / `clang-sanitizer.yml` | ✅ 容器 | 需要下载 clang-20 / sanitizer 运行，未实测 |
| `build.yml` ubuntu / android / emscripten | ✅ 容器 | android/emscripten 需下载 NDK/SDK/emsdk，很慢，未实测 |
| `build.yml` `windows-latest` | ⚠️ host 模式 | 本机是 Windows 11 + VS2022，GitHub 是 Windows Server 2022，预装工具不同；未实测 |
| `build.yml` `windows-latest-mingw` | ⚠️ host 模式 | 需要 `gcc/g++/windres` 在 PATH（本机在 `C:\Code\Env\gcc-*`，默认不在） |
| `fast-build.yml` ubuntu-22.04 / build-android | ✅ 容器 | 需对应镜像与大量下载，未实测 |
| `debug-build.yml` | ⚠️ 部分 | Linux 容器可跑；Windows host 可跑；macOS 不可 |
| `build.yml` / `fast-build.yml` 的 macOS、iOS 相关作业 | ❌ | 需要 macOS + Xcode |
| `release.yml` | ⚠️ 部分 | 依赖 secrets（`BESTCLIENT_OFFICIAL_CODE`、签名证书），只能本地 dry-run |
| `codeql-analysis.yml` / `static.yml` / `update-flake.yml` / `build-libraries-*.yml` | ⚠️/❌ | 依赖 GitHub 服务（CodeQL、Pages、cachix 等），不建议本地复刻 |

**无法 1:1 的部分**：

- GitHub-hosted runner 镜像差异（Linux catthehacker ≠ 官方镜像；Windows Server vs 本机 Windows 11；macOS）。
- `secrets` 内容、`GITHUB_TOKEN` 权限、artifact/cache/Pages/release 等平台服务（act 用本地 HTTP server 近似）。
- 并发策略、缓存命中、runner 资源（CPU/内存/超时）与 GitHub 账单/配额。
- `paths-ignore` 等事件过滤：act 不执行，本地按步骤执行（暂停 CI 期间这是预期行为）。
- Windows host 模式会在本机安装 workflow 声明的工具（如 Vulkan SDK 由 `humbletim/install-vulkan-sdk`
  安装/缓存），本机环境因此可能被改变；GitHub runner 是一次性环境。

## 已知问题与处理

1. **首次 `git clone <action>` 偶发超时**（Docker Desktop → GitHub 网络抖动）
   - 脚本默认 `--action-offline-mode`：动作已缓存在 `%USERPROFILE%\.cache\act` 时不再联网；
   - 未缓存的动作仍会拉取，失败时脚本按日志特征自动重试一次（`-MaxAttempts`）；
   - 想强制刷新动作：`-RefreshActions`。
2. **artifact server 端口**：act 默认 34567，多个 act 并行会冲突；脚本每次用系统分配的
   空闲端口。若仍报 `bind: ... in use`，脚本会自动换端口重试。
   （历史 bug：`--artifact-server-port` 为空会让 `actions/upload-artifact` 报
   `ECONNREFUSED` / `Unexpected end of JSON input`，已修复。）
3. **启动慢（复制仓库）**：仓库含 submodule 约 4 GB，每次容器作业要 `docker cp` 约 3 分钟；
   `-Bind` 可跳过复制（只读作业可用）。
4. **host 模式 workspace 残留**：Windows 上 act 清理常因文件占用失败，单次约 5 GB
   （`%USERPROFILE%\.cache\act\<16hex>`）。脚本结束后自动清理；`-KeepHostWorkspaces` 可保留。
5. **不要同时跑多个 host 模式 act 实例**（同一仓库）：会争用 CPU/磁盘和 act workspace；
   `-ConcurrentJobs` 只限制单次 act 内的并发。
6. **本地工作树会被验证**：act 用本地副本代替 checkout，未提交改动也会参与；
   如果要验证"干净 clone"，请先 commit 或使用单独的 worktree。

## 日志与产物

- 日志：`_work/ci/act/logs/<workflow>-<event>[-<job>][-<matrix>]-<时间>.log`
  （重试时追加 `-attempt2`）。
- artifacts：`_work/ci/act/artifacts/<runId>/<artifact-name>/...`
- cache：`_work/ci/act/cache`（act cache server；如 Vulkan SDK 安装包缓存）。
- 结论与对比：`_work/ci-local/report.md`。

## 验证记录（2026-11-11）

命令与结果（均执行仓库内真实 workflow YAML，未改动 workflow）：

```text
# Linux：rust.yml rustfmt（ubuntu-latest，扩展 runner 镜像）
.\scripts\ci-local\run-ci-local.ps1 -Workflow rust -Job rustfmt -UseCustomRunner
结果: PASS，~4m16s，🏁 Job succeeded
日志: _work/ci/act/logs/rust-push-rustfmt-20261111-170110.log
要点: checkout 由 act 用本地副本代替(3m16s) → Swatinem/rust-cache@v2.9.2 → cargo fmt -- --check
      Success[207ms] → post rust-cache save(46.9s)

# Windows：fast-build.yml build-cmake windows-latest（host 模式）
.\scripts\ci-local\run-ci-local.ps1 -Workflow fast-build -Job build-cmake -Matrix os:windows-latest
结果: PASS，~6m15s，🏁 Job succeeded
日志: _work/ci/act/logs/fast-build-push-build-cmake-os_windows-latest-20261111-165448.log
要点: Vulkan SDK 命中缓存(install 20.1s) → Release 构建成功(2m37s) → package_default(15s)
      → upload-artifact ddnet-windows-latest.zip(65,058,301 B, 9.0s)
产物: _work/ci/act/artifacts/1/ddnet-windows-latest/ddnet-windows-latest.zip

# artifact server 修复的定向验证（最小 workflow）
workflow: _work/ci-local/act-smoke/artifact-port-test.yml
修复前（--artifact-server-port 为空）: FAIL - upload-artifact ECONNREFUSED / Unexpected end of JSON input
修复后（脚本自动分配空闲端口）: PASS - artifact 落盘 _work/ci/act/artifacts/1/smoke/smoke.zip

# 修复前对照：Windows fast-build 首次运行（除 Upload Artifacts 外全部成功）
日志: _work/ci/act/logs/fast-build-push-20261111-164231.log
说明: artifact server 端口为空导致上传失败；打包产物已转存
      _work/ci-local/evidence/lead-first-run-bestclient-3.1.1-win64.zip
```

完整报告（含根因分析、harness 改动清单、残余风险、不可复刻项）：`_work/ci-local/report.md`。

## 暂停 CI 期间

仓库当前的 `TEMP(quota)` 配置只影响 GitHub 上的触发，不影响本地 act。
配额恢复后 `git revert` 暂停提交即可重新开启 CI，之后可继续用本脚本先本地验证。
