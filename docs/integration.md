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
| 示例：占位 | RanbiClient / `src/game/client/components/ranbi/...` | `src/game/client/components/<source>/...` | 待评估 | — | 示例行，整合开始后替换 |

## 整合批次

- **第一批**：待用户确认（建议从渲染/观感类低风险功能开始）。
- **后续批次**：见 RanbiClient 功能清单分析报告（`_work/ranbi-inventory/report.md`，本地工作区）。

## 流程

1. Lead 用 `team_task_create` 建任务（来源定位、目标位置、验收、write_scopes）。
2. `source-analyst` 分析来源实现，`porter` 移植，`verifier` 独立验证。
3. Lead 合并、构建、review 后自动 commit（不 push）。
4. 用户实机验证后，状态更新为「已完成」。
