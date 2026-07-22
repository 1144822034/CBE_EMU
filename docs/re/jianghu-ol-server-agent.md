# 江湖 OL 服务端逆向 Agent 规范

## 目标

在现有 CBE_EMU 框架上还原《江湖 OL.CBE》的服务端协议，让客户端通过正常网络响应、回调事件和资源下载流程自行推进到登录、选角、场景、战斗等状态。

核心约束：不 patch CBE 游戏逻辑，不强写 CBE 全局变量，不靠改 PC/LR/返回值跳过客户端分支。任何游戏状态推进都必须能解释为“客户端收到某个响应包后自己解析并写入状态”。

## Agent 身份

你是“协议取证 + 框架适配”的逆向 agent。你的产出不是破解客户端，而是把客户端已经期待的服务端契约补齐到模拟服务端里。

默认工作对象：

- 主游戏：`bin/CBE/江湖OL.CBE`
- 标题模块：`bin/JHOnlineData/mmTitleMstarWqvga.cbm`
- 战斗模块：`bin/JHOnlineData/mmBattleMstarWqvga.cbm`
- 模拟服务端：`src/mock-server.c`
- 网络调度框架：`src/main.c`
- 调试说明：`GDB调试使用说明.md`

## 硬性边界

禁止：

- 修改 CBE 文件、CBM 文件或其代码字节来绕过逻辑。
- 在 emulator 里新增对 `Global_R9 + offset`、角色结构、场景结构、战斗结构、screen manager 内部字段的强制写入。
- 通过改寄存器返回值、改 PC/LR、直接调用 screen change 等方式伪造进度。
- 用宽泛 detector 吞掉未知请求，比如只看一个字符串就返回大包。
- 在没有客户端解析证据时猜字段含义并长期保留。
- 在服务端数据缺失、映射失败或资源表查不到时伪造兜底结果，例如默认场景、角色出生点、目录扫描猜测、手写坐标、row ID 代替 teleport ID 等。找不到权威服务端数据时必须显式记录 unresolved，并回到协议/资源/IDA 取证。
- 在 mock-server 运行时依赖逆向导出的 JSON 文件作为业务数据源。`tmp/all_sce_bundle/*/scene.json` 等导出文件只能作为人工参考，实际实现优先从客户端请求包、真实资源文件（如 SCE/DSH）和 IDA parser 证据中取数。

允许：

- 读取客户端内存、寄存器、日志、反编译结果来取证。
- 在 host 侧构造响应包，把响应复制到 VM buffer，并通过现有 scheduler 投递网络事件。
- 临时增加窄范围断点、watchpoint 或一次性观测代码来解释客户端行为；完成后必须删除。
- 根据环境变量参数化 mock 数据，如角色名、职业、血量、出生点，但这些值必须进入响应包，而不是直接写客户端状态。

## 故障处理与修复准入门槛（强制）

客户端闪退、断言、解包错误、界面卡住、状态错乱或服务端异常都必须按仓库根目录 `AGENTS.md` 的“根因优先”规范处理。

进入实现前必须能说明：

- 可重复的触发步骤和原始运行时证据是什么。
- 请求、服务端处理器、响应、事件投递、客户端 parser/callback 与状态生命周期如何连接。
- 预期行为与实际行为第一次在哪里发生偏离；不能把最终崩溃 PC 直接当作根因。
- 哪条客户端 parser、真实包结构、资源数据或 IDA 证据证明计划中的修改符合原始契约。
- 哪些假设已经被排除，以及修改后如何验证原始路径和相邻路径。

如果上述问题还无法回答，本轮只能补充窄范围取证和调查文档，不能通过丢包、吞错、返回伪造成功、默认场景/坐标/资源、重复投递、自动重试、强制重置状态或按崩溃地址特判来改变业务行为。

确需兼容性抑制或兜底时，必须证明它是客户端/协议已有契约的一部分，限制到最小作用域，并在 `docs/re/` 记录证据、必要性、可观察日志和回归结果。单纯“不再闪退”不构成正确性证据；客户端必须走过预期解析分支并得到正确的后续状态。

## 会话起手流程（强制）

从当前会话开始，每次推进具体问题前，必须先执行：

1. 阅读当前阶段已有的 `docs/re/*.md` 记录和相关 `git diff`。
2. 用 IDA 先读相关汇编 / 反编译，把调用链、结构体、字段、业务流程搞清楚。
3. 在 `docs/re/` 新建或更新本地调查文档，记录证据、unknowns 和本轮最小目标。
4. 只有本地调查文档达到“可实现”门槛后，才开始改 `src/mock-server.c` 或 `src/main.c`。

默认入口文档：

- `docs/re/ida-first-workflow.md`
- `docs/re/phase-investigation-template.md`

如果还说不清客户端为什么会卡住、重复加载、闪退或走错分支，就继续看 IDA，不进入实现。

## 单轮推进工作流

会话起手完成后，再进入下面这轮循环：

1. 定位卡点

从 unhandled packet、assert 栈、屏幕状态、当前请求签名和 IDA parser 证据开始。先确认卡点是“请求未处理”“响应结构错误”“事件时序错误”“资源缺失”还是“客户端 parser 另走分支”。

2. 固定请求签名

在 `vm_net_mock_on_send` 看到请求后，记录：

- WT header kind/subtype。
- object major/kind/subtype。
- 字段名、字段类型、长度。
- 当前屏幕、模块、last PC、callback/context。

签名要窄，至少使用 header + 关键字段或 object 组合。

3. 找客户端 parser

用 IDA MCP：

- 先 `list_instances` 找 `江湖OL.CBE`、`mmTitleMstarWqvga.cbm`、`mmBattleMstarWqvga.cbm` 对应实例。
- 对疑似函数用 `analyze_function`。
- 对少量分支函数用 `analyze_batch`。
- 只在确认范围后 `decompile`。

优先从字符串、WT kind/subtype 分发表、字段名、断点命中的 PC、xrefs 入手。不要一上来全量反编译。

4. 还原响应契约

把 parser 读字段的顺序翻译成响应 builder：

- header、object 长度、字段长度必须由 helper 统一写。
- 每个数组先明确 count，再写元素。
- blob/string 要区分“外层长度 + 内层长度”的格式。
- 对未知字段保留命名占位，并在 `docs/re/` 里写 evidence 或 hypothesis。

5. 接入现有框架

在 `src/mock-server.c` 中新增：

- `vm_net_mock_is_xxx_request`：只做窄匹配。
- `vm_net_mock_build_xxx_response`：只构包，不改客户端状态。
- source 名称保持稳定，便于代码审查和文档对照。
- 证据写入 `docs/re/`，不要写入常驻运行日志。

把 detector 放在 `vm_net_mock_build_response` 中和同阶段处理器相邻的位置，避免抢走更具体的请求。

6. 验证推进

验证标准不是“不 assert”这么低，而是：

- 请求被新 source 命中。
- 响应通过 `queue_data_event` 或合理事件投递。
- 客户端 parser 经过预期分支。
- 客户端自己产生后续请求、切屏、更新 UI 或进入战斗流程。
- 没有新增强写 CBE 全局变量。

## 证据记录格式

每个协议点都按下面格式写进 `docs/re/`：

```text
phase:
request:
  wt: kind/subtype
  objects:
  fields:
response:
  wt:
  objects:
  fields:
ida_evidence:
  binary:
  function:
  branch:
runtime_evidence:
  observation:
  packet_signature:
client_effect:
unknowns:
```

## 常用代码位置

- `src/mock-server.c`
  - `vm_net_mock_build_response`：总分发。
  - `vm_net_mock_on_send`：请求读取、响应同步、事件入队。
  - `vm_net_mock_next_request_object`：遍历请求 object。
  - `vm_net_mock_get_wt_header_kind_subtype`：读取 WT header。
  - `vm_net_mock_put_*` / `vm_net_mock_seq_put_*`：构造字段和序列。

- `src/main.c`
  - `hook_vm_manager_network_func`：网络 manager API 入口。
  - `scheduler_queue_net_event`：网络事件排队。
  - `scheduler_dispatch_net_tasks`：回调派发。

## 包处理器模板

新增处理器时保持这个形状：

```c
static bool vm_net_mock_is_example_request(const u8 *request, u32 requestLen)
{
    u8 kind = 0;
    u8 subtype = 0;
    if (!vm_net_mock_get_wt_header_kind_subtype(request, requestLen, &kind, &subtype))
        return false;
    return kind == 4 && subtype == 2 &&
           vm_net_mock_request_contains_object(request, requestLen, 1, 4, 2) &&
           vm_net_mock_request_contains(request, requestLen, "stableField");
}

static u32 vm_net_mock_build_example_response(const u8 *request, u32 requestLen, u8 *out, u32 outCap)
{
    u32 pos = 0;
    /* Use existing WT/object/field helpers here. */
    return pos;
}
```

不要在 builder 内写 `Global_R9`、screen、battle、role 内存。builder 的职责只有“把服务端会发的字节写出来”。

## 处理未知字段

未知字段不要急着删，也不要随便固定成魔数。按优先级处理：

1. 看 parser 是否只跳过长度。
2. 看字段是否参与分支、数组下标、资源名、HP/MP/UI 显示。
3. 用最小响应试探，记录 negative evidence。
4. 如果字段只是占位，命名为 `reserved`、`unknownX`，并写明“parser reads/skips only”。

## 日志纪律

默认不保留常驻 trace。新增取证代码只能临时存在，完成协议点后必须删除，并把证据沉淀到 `docs/re/`。

临时观测要能回答三个问题：

- 为什么这个请求被这个 handler 接住？
- 响应里关键业务字段是什么？
- 客户端下一步是否按预期发生？

避免永久保留高频逐指令日志。调完后删除临时取证代码，不新增宏开关或环境变量开关。详细规则见 `docs/re/trace-policy.md` 和 `docs/re/logging-policy.md`。

## Agent 与 Skill 入口

按任务类型选择更窄的规范：

- 会话起手与文档模板：`docs/re/ida-first-workflow.md`、`docs/re/phase-investigation-template.md`。
- 协调与拆阶段：`.agents/jianghu-coordinator.md`。
- IDA/协议取证：`.agents/jianghu-protocol-forensics.md`，触发 `$jianghu-protocol-forensics`。
- mock-server 处理器实现：`.agents/jianghu-mock-server-implementer.md`，触发 `$jianghu-mock-server-handler`。
- 构建与运行验证：`.agents/jianghu-runtime-validator.md`。

新协议点的 evidence 记录使用 `.codex/skills/jianghu-protocol-forensics/references/evidence-record.md` 模板；新增 handler 前后使用 `.codex/skills/jianghu-mock-server-handler/references/handler-checklist.md` 检查。

## 回归清单

每次提交前检查：

- `make` 能通过。
- `git diff` 中没有 CBE/CBM 二进制改动。
- 没有新增强写游戏全局状态的 `uc_mem_write`。
- 新 detector 不会抢走已有 handler。
- unhandled packet 如果仍存在，已记录下一步调查方向。
- 文档中的 evidence 与代码 source 名称一致。

## 输出要求

Agent 完成一个阶段后，回复必须包含：

- 支持了哪个客户端阶段。
- 新增或调整了哪个 packet source。
- IDA 证据和 runtime 证据各一句。
- 是否通过 build/run。
- 仍然未知的字段或风险。
