# AGENTS.md — 整合客户端开发约定（智能体团队）

本仓库是**整合客户端**主仓：以 **BestClient** 为底座，把多个 DDNet/TClient 系客户端的功能移植、整合进来。
本文件面向在本仓库工作的所有 AI 智能体（包括 Team Lead 与 teammate）。
**非 trivial 的整合工作必须使用智能体团队（Agent Teams）完成，不要由 Lead 单人猜改、单人提交。**

术语：

- **底座**：根目录下的客户端代码主线（源自 BestClient，基于 DDNet/TClient）。
- **来源**：`clientsSource/<Name>/` 下由 git submodule 固定的只读上游客户端源码。
- **移植**：把来源中的功能实现按底座版本重写并合入根目录。
- **验收**：构建 0 error、独立 verifier 复核、用户实机验证，三者缺一不可。

## 1. 强制要求

任何**非 trivial 的整合工作**（新功能移植、跨版本 API 适配、设置页/配置变量、渲染/计分板/聊天等行为改动、协议或服务端配合、构建脚本、数据/翻译）都必须：

1. 由 **Team Lead** 建共享任务（`team_task_create`），写清楚：
   - 目标行为，以及来源定位（来源客户端目录、文件名与行号、函数/类名、配置名、控制台命令）；
   - 根目录目标位置（组件/文件/配置 domain）；
   - 验收标准（构建、回归点、实机预期现象）；
   - 写入范围（`write_scopes`，如 `src/game/client/components/ranbi/`、`src/engine/shared/config_variables_ranbi.h`、`_work/<task>/`）。
2. 由 Lead **spawn/指派 teammate**（`spawn_teammate`、`send_message`）：
   - 来源分析：`source-analyst`（可按客户端细分，如 `ranbi-analyst`）；
   - 移植实现：`porter`；
   - 独立验证：`verifier`（必须独立，不能是补丁作者）。
3. 多个独立功能可并行；同一文件的写入拆成不同任务或不同时间窗口，`write_scopes` 不重叠。
4. teammate 在 `_work/<task>/` 下交付：
   - `report.md`（来源文件与行号、改动点、风险、验证步骤）；
   - `patch.diff`（Lead 可直接应用）；
   - 构建/运行证据。
   teammate **不要提交 git、不要修改 `clientsSource/`、不要越出 `write_scopes`、默认不要启动游戏**。
5. Lead 负责：审查报告 → 合并到根目录 → 构建 → 独立 verifier 复核 → 同步文档 → commit → 给用户实机验证清单。
6. **必须等 verifier 给出 PASS/FAIL + 风险清单后**才能把任务标记完成；Blocker 未关闭不得声称“已整合”。
7. **整合范围由用户指定或确认**；Lead 不要自行挑选大批功能开始移植。

Trivial 改动（纯错别字、注释、仅文档格式）可由 Lead 直接处理，但不得把 trivial 当作跳过团队做功能/移植的理由。

## 2. 团队协作流程

推荐顺序：

```text
Lead:
  team_task_create（行为需求 + 来源定位 + 目标位置 + 验收 + write_scopes）
  spawn_teammate / send_message 指定 owner
  wait_agent 等进展
  team_task_get → 审阅 report/patch
  Lead 合并改动到根目录
  Lead: cmake 构建 0 error + smoke
  verifier task: 独立 clean build + 回归检查
  PASS 后：同步文档/翻译/changelog
  Lead: 自动 commit（只提交，不 push）
  最后给用户实机验证清单（步骤 + 预期现象 + 日志/控制台关键字）
```

约定：

- 任务状态用 `team_task_update` 的 `claim` / `complete`；Lead 用 `reassign` 调整 owner。
- 每个任务只允许改自己 `write_scopes` 的范围；跨范围先改任务描述或新建任务。
- 一个任务只整合一个功能闭环（定位 → 移植 → 构建 → 验证 → 文档）；跨功能重构单独开任务。
- teammate 的报告必须给出**证据**（来源文件行号、函数名、配置名、对比截图），不能只给结论。
- Lead 必须把结论、残余风险、实机验证步骤同步给用户；不能把 verifier PASS 当成实机验证。

## 3. 仓库结构与来源客户端

- 根目录 = 整合客户端代码、文档、构建脚本的主仓；git 只跟踪这里的内容。
- `clientsSource/` = 只读的上游客户端源码，以 **git submodule** 管理（`.gitmodules` 记录固定 commit，`.gitignore` 不再忽略）。**只读：不修改、不在其中提交或切分支**；来源更新由用户确认后更新 submodule 指针（单独任务）。
- 新增来源客户端：`git submodule add <url> clientsSource/<Name>`，在下方登记表登记；不要直接把源码复制进根仓库。
- **底座导入已完成**：根目录代码导入自 `clientsSource/BestClient/` @ `81ca85f2b`（BestClient 3.1.1，DDNet 20.1.1，TClient 10.9.0）；后续所有开发都在根目录进行。
- 来源客户端登记：

| 客户端 | 版本 | 上游基线 | submodule 路径 | 用途 |
| --- | --- | --- | --- | --- |
| BestClient | 3.1.1 | DDNet 20.1.1 / TClient 10.9.0 | `clientsSource/BestClient` | 底座 |
| RanbiClient | 1.0.2 | DDNet 19.8 / TClient 10.8.7 | `clientsSource/RanbiClient` | 功能来源 |

- 版本以各来源 `src/game/version.h` 为准；来源同步后更新本表。
- 功能清单看各来源的 `README.md`（如 RanbiClient 的勾选功能表），不要在本文件复制功能列表。
- 来源与底座的上游版本不同（如 RanbiClient 基于 DDNet 19.8，底座是 20.1.1）：先对照底座对应实现，按底座 API/类型/命名适配；整文件复制旧版会带回旧 API 和旧配置。

## 4. 功能整合规则（代码）

- 新增独立模块放 `src/game/client/components/<source>/`（如 `components/ranbi/`），并在 `CGameClient` 注册（成员 + `m_apComponents`）以及必要的初始化/hook。
- 修改底座既有文件时就地改，新增/移植行加来源标记 `// <source>`（如 `// ranbi`），沿用底座 `// bestclient`、`// tclient` 风格，便于后续比对来源。
- 配置变量：加到 `src/engine/shared/config_variables_*.h`，沿用来源前缀（如 `rc_*`；当前底座未占用 `rc_`）；需要新 domain 时同步 `config_domains.h` 与保存回调。新增后运行 `python scripts/check_config_variables.py`，确认无重名、无未使用变量。
- CMake：新文件必须加入根 `CMakeLists.txt` 的 `set_src(GAME_CLIENT ...)` 列表，并保持 `tidy-alphabetical` 字母序；加文件后重新执行 cmake 配置。
- 翻译：界面字符串用 `Localizable()`；新增/修改后在根目录运行 `python scripts/languages/validate.py` 和 `python scripts/languages/update_all.py`，CI 要求 update 后无 diff。
- 资源放 `data/`（生成类放 `datasrc/`）；不要把 `clientsSource/` 当作构建输入。
- 网络/协议：默认不改 DDNet 协议；需要新消息或服务端配合时，先开独立任务评估兼容性与风险，并让用户确认。
- 保持来源功能语义，不顺手增强自动化或规则敏感行为；可能被服务器判定为违规的功能，先与用户确认再整合。

## 5. 构建、运行与验证

当前开发/验证平台：Windows 10/11 + Visual Studio 2022（C++ 桌面开发）；上游代码本身跨平台，其他平台构建按上游文档。依赖：CMake、Ninja、Rust stable（含 cargo，MSRV 1.85）、Python 3。Ninja 构建需在 VS Developer PowerShell（或已执行 vcvars 的终端）中运行；Rust 的 shims 位于 `%USERPROFILE%\.cargo\bin`，该目录必须在 PATH（本机已配置）；若缺失，可临时把 `%USERPROFILE%\.rustup\toolchains\stable-x86_64-pc-windows-msvc\bin` 放到 PATH 最前。

首次准备（根目录）：

```powershell
git submodule update --init ddnet-libs
# 需要阅读来源客户端源码时再初始化（只读参考，构建不需要）：
git submodule update --init clientsSource/BestClient clientsSource/RanbiClient
```

`ddnet-libs` 子模块必须就位，否则依赖找不到；Rust 不在 PATH 时 CMake 配置会直接报错。

构建（根目录，构建选项以导入后的 `README.md` / `docs/BUILDING.md` 为准）：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDEV=ON -DDOWNLOAD_GTEST=ON
cmake --build build --target everything -j 8
```

- **本机并行度上限 8**：本地构建、格式化、脚本等 CPU 密集型任务必须显式限制并行数——`cmake --build ... -j 8`（Ninja 默认吃满全部核心，禁止不带 `-j` 裸跑）、`python scripts/fix_style.py -j 8`；Docker/其他批量任务同理。不得占满用户机器的 CPU 与磁盘。
- **一个 build 目录只对应一套工具链**：MSVC 与 GCC/MinGW 各用独立目录（如 `build-msvc`、`build-gcc`、`build-vscode`），不要在同一 build 目录切换编译器/generator。GCC/MinGW 构建必须使用 GNU 版 Rust（`RUSTUP_TOOLCHAIN=stable-x86_64-pc-windows-gnu`），否则 cargo 产出 MSVC 的 `.lib`，链接时会找不到 `libddnet_engine_shared.a`。

- 只编客户端：`cmake --build build --target game-client`；产物 `build\DDNet.exe`（`CLIENT_EXECUTABLE` 默认 `DDNet`；`DDNet` 同名 target 只在 Makefile 生成器下存在）。
- 也可用 VS 生成器：`cmake -S . -B build -A x64`，构建时加 `--config Debug`（CI Windows 用此法）。
- 运行：在**根目录**执行 `.\build\DDNet.exe`（客户端优先从当前目录的 `data/` 读资源）。
- 测试与检查：

```powershell
cmake --build build --target run_tests
python scripts/fix_style.py --dry-run        # 需要 clang-format 20
python scripts/check_config_variables.py
python scripts/languages/validate.py
```

验收至少包含：

1. **0 error** 编译（记录 warning 是否为既有）；
2. 改动功能对应的回归点自查（同一文件的既有行为未被破坏）；
3. verifier 独立 clean build（重新配置，不复用缓存）；
4. 用户实机验证（Lead 给出精确步骤、预期现象、日志/控制台关键字；双开/联机场景写清客户端版本）；
5. 文档同步：`README.md` 功能表、`docs/integration.md`（整合登记/进度）、翻译/配置说明（受影响时）。

## 6. 版本与 Git

- **Lead 在完成验证后自动 commit，无需每次询问；本仓库只 commit，不 push**——推送只由用户执行。
- 根仓库 `main` 为整合主线；功能开发按 Lead 判断使用 `feat/<name>` 分支，完成后合回。
- `.gitignore` 必须忽略：`build*`、`_work/`、生成产物（如 `src/generated`、Rust 桥接产物）；`clientsSource/` 由 git submodule 跟踪，不再忽略。缺失时 Lead 在任务中补齐。
- 客户端名称、版本号、README 版本只在用户明确要求时改；未经要求不提升、不改名。
- 禁止 `git tag`、改写历史、force-push（除非用户明确要求并确认）。
- commit 使用 `port(<source>): ...`（功能移植）、`fix(...)`、`docs(...)`、`chore(...)` 前缀；正文写来源文件与版本、目标位置、验证结果、风险、用户实测步骤。
- 一个 commit 对应一个功能闭环或一个可独立回滚的改动；不要把多个来源的无关移植混在一起。
- commit 前确认 `git status` 干净、暂存内容不含构建产物或未预期的 submodule 指针变更。

## 7. Windows / 编码

- Windows 优先；源码 UTF-8、LF，遵循 `.editorconfig` / `.clang-format`；不要批量改写换行、编码或 BOM。
- C++ 格式化交给 `scripts/fix_style.py`（clang-format 20），不要手工调对齐。
- 游戏进程可能锁 `DDNet.exe`：先关客户端再构建；构建失败要报告，不要静默跳过。
- PowerShell 下中文输出可能乱码：必要时 `chcp 65001`，以英文构建日志和退出码作为事实依据。
- 调试探针只用于定位问题，测完恢复正式实现；探针不改变功能行为（除非任务明确说明）。

## 8. 安全边界

- 不修改用户未授权的游戏安装、存档、系统配置；`clientsSource/` 只读。
- 不把第三方客户端源码、二进制、游戏数据提交到根仓库；新增依赖先确认来源与许可证。
- 读取用户日志/截图属于诊断需要；最终回复只引用必要行，不扩散隐私。
- 不擅自打包、发布或上传构建产物；发布相关操作等用户明确要求。

## 9. 完成定义（Definition of Done）

一个功能只有同时满足以下条件才算整合完成：

- [ ] 团队任务已建立、owner 明确，report/patch/证据已交付；
- [ ] 移植按底座版本适配，来源标记齐全，未整文件覆盖旧版代码；
- [ ] 根目录构建 0 error（既有 warning 已标注）；
- [ ] verifier 独立复核 PASS（clean build + 回归检查/相关脚本通过）；
- [ ] `README.md` / `docs/integration.md` / 翻译 / 配置说明已同步；
- [ ] 已自动 commit（未推送，何时推送由用户决定）；
- [ ] 给用户的实机验证步骤、预期现象、残余风险已写清。

本文件是团队约定的唯一来源；流程或来源客户端变化时先更新本文件。
