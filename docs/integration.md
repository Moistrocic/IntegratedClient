# 整合台账（Integration Registry）

本文件记录「来源客户端功能 → 底座（BestClient）实现」的整合进度与验收状态。
项目结构与来源客户端约定见 [AGENTS.md](../AGENTS.md)。

## 状态图例

| 状态 | 含义 |
| --- | --- |
| 待评估 | 尚未做来源定位/移植评估 |
| 已评估 | 已有来源定位、改动点与风险清单，等待排期 |
| 移植中 | 有 owner 正在实现 |
| 待验证 | 已实现并本地构建，等待 verifier 复核 |
| 待实机 | verifier 已 PASS，等待用户实机验证 |
| 已完成 | 构建、verifier、实机验证全部通过 |
| 阻塞 | 有明确 blocker（记录在备注） |

> 所有 Ranbi 功能默认关闭（默认 0），关闭时与底座 BestClient 行为一致；
> 高风险功能（自动换肤 / 全图玩家 / 记分板改名 / AI）另带 UI 风险提示与二次确认。

## 功能登记

| 功能 | 来源（客户端 / 位置） | 底座目标位置 | 状态 | 负责人 | 备注（commit） |
| --- | --- | --- | --- | --- | --- |
| UI/品牌改造：自动更新移除、主菜单按钮清理、启动页四行版本、Ranbi 顶级设置页签框架 | 本项目需求 | `menus_start.cpp`、`menus_bestclient.cpp`、`settings_nav.*`、`components/ranbi/menus_ranbi.cpp` 等 | 待实机 | ui-porter | `9d04017`；verifier PASS（task-6） |
| Info 恢复原样 + 新增顶级 IntegratedClient 设置页签 | 用户修正 | `bestclient/menus_bestclient.cpp`、`settings_nav.*`、`components/integratedclient/menus_integratedclient.cpp` | 待实机 | ui-porter | `3ea5625`；verifier PASS（task-8） |
| Ranbi 批次 0：`RANBI`/`RANBIWEAPONS` 配置域、60 个 `rc_*`、`RCLocalize` | RanbiClient 1.0.2 / `config_variables_ranbi.h`、`config_domains.h` | `src/engine/shared/config_variables_ranbi.h` 等 | 待验证 | ranbi-porter | `64bd03a` |
| Ranbi 批次 1：分身切换修复（`rc_hammer_fix` / `rc_cursor_copy`） | RanbiClient / `controls.cpp`、`gameclient.cpp` | `components/controls.cpp`、`gameclient.cpp` | 待验证 | ranbi-porter | `64bd03a`；默认关闭 |
| Ranbi 批次 2：表情/辅助线滞后渲染 | RanbiClient / `render_proxy.cpp`、`players.cpp`、`nameplates.cpp` | `components/ranbi/render_proxy.*` 等 | 待验证 | ranbi-porter | `2848a6a`；与来源有意差异 D1 已记录 |
| Ranbi 批次 3：武器角度标记 + 最近武器 | RanbiClient / `weapons.cpp` | `components/ranbi/weapons.*` | 待验证 | ranbi-porter | `c5a517e`；`RANBIWEAPONS` 保存回调 |
| Ranbi 批次 4：Nameplates 扩展 + matched 高亮 | RanbiClient / `nameplates.cpp`、`bestclient/show_points.*` | `components/nameplates.cpp`、`bestclient/show_points.*` | 待验证 | ranbi-porter | `7b23bc5`；分数复用底座 CShowPoints |
| Ranbi 批次 5：Ranbi 设置页 GUI 内容填充 | RanbiClient / `menus_ranbi.cpp` | `components/ranbi/menus_ranbi.cpp`、`settings_search_catalog.inl` | 待验证 | ranbi-porter | `486f94a`；含 35 条搜索登记 |
| Ranbi 批次 6：聊天历史去重/聊天分数/旁观皮肤名/三格水提示 | RanbiClient / `chat.cpp`、`hud.cpp` | `components/chat.cpp`、`hud.cpp` 等 | 待验证 | ranbi-porter | `213a3cd`；三格水为独立实现 |
| Ranbi 批次 7：高风险功能（自动换肤/全图玩家/记分板改名） | RanbiClient / `gameclient.cpp`、`scoreboard.cpp` | `gameclient.cpp`、`scoreboard.cpp` | 待验证 | ranbi-porter | `57f1ffb`；默认关闭 + 风险提示 + 二次确认 |
| Ranbi 批次 8：AI 自动回复（默认关闭） | RanbiClient / `ai_client.cpp` | `components/ranbi/ai_client.*`、`RANBI_TAB_AI` | 待验证 | ranbi-porter | `0593901`；Token 明文/隐私/规则警告；仅建议测试服务器验证 |
| R3：`CShowPoints` 并发获取加速 + 并发上限输入框 | RanbiClient `points.cpp` 思路 + 用户需求 | `bestclient/show_points.*`、`menus_bestclient.cpp` | 待验证 | ranbi-porter | `7aa938a`；`bc_show_points_max_concurrency` 默认 5（1–5） |
| 本地 GitHub Actions 复刻（Linux + Windows） | CI 配额问题 | `scripts/ci-local/` | 已完成 | ci-validator | `35b042c`；act + catthehacker/扩展 runner，Linux/Windows PASS |

## 整合批次

- **批次 0（基础设施）**：Ranbi 配置域/配置变量、`RCLocalize`、翻译并入 `data/languages`、构建登记 — `64bd03a`。
- **批次 1（分身切换修复）**：`rc_hammer_fix` + `rc_cursor_copy` — `64bd03a`。
- **批次 2（渲染）**：表情/辅助线滞后渲染 — `2848a6a`。
- **批次 3（武器）**：武器角度标记 + 最近武器 — `c5a517e`。
- **批次 4（Nameplates）**：分数/完成/状态/范围隐藏 + matched 高亮 — `7b23bc5`。
- **批次 5（Ranbi GUI）**：设置页内容 + 搜索登记 + 中文翻译 — `486f94a`。
- **批次 6（聊天/HUD）**：历史去重、聊天分数、旁观皮肤名、三格水提示 — `213a3cd`。
- **批次 7（高风险，默认关闭）**：自动换肤、全图玩家、记分板改名 — `57f1ffb`。
- **批次 8（AI，默认关闭）**：AI 自动回复 + `RANBI_TAB_AI` — `0593901`。
- **R3（分数并发）**：`bc_show_points_max_concurrency` + 并发加速 — `7aa938a`。
- 来源分析报告：`_work/ranbi-inventory/report.md`（本地工作区，778 行）。
- 各批次移植报告：`_work/ranbi/batch*/report.md`（本地工作区）。

## 流程

1. Lead 用 `team_task_create` 建任务（来源定位、目标位置、验收、write_scopes）。
2. `source-analyst` 分析来源实现，`porter` 移植，`verifier` 独立验证。
3. Lead 合并、构建、review 后自动 commit（不 push）。
4. 用户实机验证后，状态更新为「已完成」。
