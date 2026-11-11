# 整合台账（Integration Registry）

本文件记录「来源客户端功能 → 底座（BestClient）实现」的整合进度与验收状态。
项目结构与来源客户端约定见 [AGENTS.md](../AGENTS.md)。

## 状态图例

| 状态 | 含义 |
| --- | --- |
| 待评估 | 尚未做来源定位/移植评估 |
| 已评估 | 已有来源定位、改动点与风险清单，等待排期 |
| 移植中 | 有 owner 正在实现 |
| 待验证 | 已实现并本地构建，等待 verifier 复核/用户实机验证 |
| 已完成 | 构建、verifier、实机验证全部通过 |
| 阻塞 | 有明确 blocker（记录在备注） |

## 功能登记

| 功能 | 来源（客户端 / 文件） | 底座目标位置 | 状态 | 负责人 | 验收/备注 |
| --- | --- | --- | --- | --- | --- |
| Ranbi 基础设施（`RANBI` 配置域、60 个 `rc_*`、`RCLocalize`） | RanbiClient 1.0.2 / `src/engine/shared/config_variables_ranbi.h`、`config_domains.h` | `src/engine/shared/config_variables_ranbi.h`、`config_domains.h`、`config_includes.h`、`src/game/localization.*` | 待验证 | ranbi-porter | task-5；配置域追加末尾；翻译已确认并入 `data/languages`（`RCLocalize` 由 `update_all.py` 提取） |
| 分身切换修复（`rc_hammer_fix` / `rc_cursor_copy`） | RanbiClient 1.0.2 / `components/controls.cpp:315-317`、`gameclient.cpp:558-560,3736-3751` | `src/game/client/components/controls.cpp`、`src/game/client/gameclient.cpp` | 待验证 | ranbi-porter | task-5；默认关闭，关闭时与底座行为一致 |

## 整合批次

- **批次 0（基础设施）**：Ranbi 配置域/配置变量、`RCLocalize`、翻译并入 `data/languages`、构建登记 — 待验证（task-5）。
- **批次 1（第一批功能）**：分身切换修复（`rc_hammer_fix` + `rc_cursor_copy`）— 待验证（task-5）；表情/辅助线滞后渲染、武器角度标记为后续批次。
- **后续批次**：见 RanbiClient 功能清单分析报告（`_work/ranbi-inventory/report.md`，本地工作区）与批次 0+1 移植报告（`_work/ranbi/batch0-1/report.md`）。

## 流程

1. Lead 用 `team_task_create` 建任务（来源定位、目标位置、验收、write_scopes）。
2. `source-analyst` 分析来源实现，`porter` 移植，`verifier` 独立验证。
3. Lead 合并、构建、review 后自动 commit（不 push）。
4. 用户实机验证后，状态更新为「已完成」。
