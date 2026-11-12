# IntegratedClient

以 **BestClient** 为底座的整合客户端项目，在 DDNet / TClient 生态基础上整合 **RanbiClient** 的功能，并新增本项目特性。当前版本：**1.0.0**。

## 来源仓库

- [**DDNet**](https://github.com/ddnet/ddnet) — 上游游戏/引擎基础
- [**TClient**](https://github.com/TaterClient/TClient) — 客户端基础功能
- [**BestClient**](https://github.com/BestProjectTeam/BestClient) — 本项目底座
- [**RanbiClient**](https://github.com/Moistrocic/RanbiClient) — 部分功能来源

## 改动/新增功能

### UI

- 移除 BestClient 自动更新（含主菜单“检查更新”按钮与整条 updater 链路）
- 移除主菜单左侧 “Try Light / 使用Light” 按钮
- 启动页右下角显示四行版本：`DDNet` / `TClient` / `BestClient` / `IntegratedClient`
- 设置新增 **Ranbi** 顶级页签（General / Nameplates / Chat & Scoreboard / Notify / Weapons / AI）
- 设置新增 **IntegratedClient** 顶级页签，展示项目信息、版本、来源客户端与仓库/整合台账链接
- BestClient 仅移除自动更新相关按钮

## BestClient

- 添加了是否向 BC 服务器发送身份的开关，关闭后你将无法使用和 BC 服务器相关功能。开启则会被 BC 服务器标记为 Fake 玩家（非官方构建 BC 端）。
- 修复 Fast Practice 开关瞬间误攻击（底座 BestClient 本身问题）：开启/关闭练习不再向服务器发送一次残留 fire，正常开火与练习内攻击不受影响。
- Fast Practice 虚影坐标：HUD 与 TClient statusbar 的 Position / Speed / Angle 可在练习时显示当前操控虚影的数据，而不是固定的实体值。

### RanbiClient 功能整合（默认关闭，关闭时与底座行为一致）

- 分身切换修复：切换分身不再意外出锤、保留准星
- 表情 / 辅助线滞后渲染（不被水面与透明方块遮挡）
- 武器角度标记 + 最近武器切换（`+switch_recent_weapon`）
- Nameplates 扩展：DDNet 分数、完成标记、Dummy Copy / Hammer Fly / Dummy Reset 状态、范围隐藏、同 X 位置高亮
- 聊天历史去重、聊天显示分数、旁观者皮肤名、跳过三格水提示
- 信息获取功能（默认关闭 + UI 风险提示）：碰撞自动换肤、全图玩家信息、计分板“获取名称”
- AI 自动回复（默认关闭 + Token/隐私/规则警告；支持 0.6/0.7）
- **分数获取替换**：Ranbi → Chat & Scoreboard 开关“替换bc的分数获取逻辑”（默认关闭）。开启后 BC 计分板 / nameplate / chat 的分数统一使用 Ranbi 的 ddnet.org 取数逻辑，**所有服务器**（含 EGO / Legit）显示 DDNet 分数；关闭则完全使用 BC 原 provider 逻辑。

### 本项目新增功能

- **DeepSeek 翻译后端**：翻译设置新增 DeepSeek（OpenAI 兼容 API），支持 Endpoint / API Key / Model 配置
- **复读上一个玩家**：Ranbi → Chat & Scoreboard 绑定快捷键，按下后复读其他玩家最近一条消息（无消息时不动作，团队消息按团队频道复读）
- **武器辅助线**：Ranbi → Weapons 开关；激光枪 / 霰弹枪 / 榴弹炮使用真实 CLaser / CProjectile 物理模拟轨迹，并复用原版发射后的光束 / 投射物尾迹渲染；不发射也会显示，随渲染帧更新
- **锁定地图完成名称**：Ranbi → General 设置；官方服务器列表的地图完成查询固定使用指定玩家名称，不受当前游戏内名称影响

## 构建与运行

依赖：Visual Studio 2022（C++ 桌面开发）、CMake、Ninja、Rust stable、Python 3。

```powershell
git submodule update --init ddnet-libs
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDEV=ON -DDOWNLOAD_GTEST=ON -DVULKAN=ON
cmake --build build --target everything -j 8
.\build\DDNet.exe
```

> 默认构建目录为 `build`，使用 **Vulkan** 后端，并同时构建客户端（`DDNet.exe`）与服务器（`DDNet-Server.exe`）。

> 本地并行度请保持在 8 核以内（详见 `AGENTS.md`）。

## 文档

- 开发约定与团队流程：[AGENTS.md](AGENTS.md)
- 整合台账（功能来源/状态/commit）：[docs/integration.md](docs/integration.md)
- 本地复刻 GitHub Actions：[scripts/ci-local/README.md](scripts/ci-local/README.md)
