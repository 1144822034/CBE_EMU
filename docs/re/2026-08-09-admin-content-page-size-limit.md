# 游戏内容管理页「超过大小限制」

日期：2026-08-09

## 症状

后台打开「游戏内容管理」时，页面显示：

`游戏内容页面超过大小限制。`

## 触发条件

1. 管理端 GET `/?tab=content`（任意已有 SCE 的场景）。
2. 当前场景含动态 NPC，和/或边缘传送点编辑表单。
3. 本地资源目录规模较大（约上千 `.actor` / `.xse`，数百任务，数百 SCE）。

## 契约与第一次偏离

- **契约**：`vm_mock_admin_render_content_page` 应产出完整可交互 HTML，并写入 `VM_MOCK_ADMIN_RESPONSE_MAX` 缓冲后返回浏览器。
- **第一次偏离**：渲染过程中 `vm_mock_admin_text_appendf` 因缓冲耗尽置 `truncated=true`；函数末尾用错误页覆盖响应，浏览器看到的不是内容管理 UI。

## 根因

内容页把完整 Actor / XSE / 场景 / 任务目录以内联 `<select><option>…</option></select>` 形式重复嵌入：

- 每个动态 NPC 表单：1×Actor + 1×XSE + 1×副本场景 + 最多 8×绑定任务 + 1×传送前置任务；
- 「增加 NPC」再复制一整套；
- 每个边缘传送点再复制一整份场景目录。

体积近似 `O((npc + portal) × catalog)`。在现有资源规模下，即使只有少量 NPC 也会超过旧的 512 KiB `VM_MOCK_ADMIN_RESPONSE_MAX`。

崩溃/错误页本身只是缓冲上限的症状；真正被违反的是「目录只应编码一次」的页面结构契约。

## 修改点

1. `src/web_admin_server.c`
   - 新增一次性 `#admin-actor-files` / `#admin-xse-files` / `#admin-scene-files` / `#admin-task-ids` datalist 渲染。
   - Actor / XSE / 副本场景 / 任务 / 传送目标场景字段改为 `input list=…`，引用上述共享目录。
   - `VM_MOCK_ADMIN_RESPONSE_MAX`：512 KiB → 2 MiB，作为 CSS、场景侧栏、预览与多 NPC 卡片的余量。
2. `src/web_admin_monsters.inc.c`
   - 怪物页同样输出 Actor datalist，供战斗 Actor 输入框使用。

## 验证

- `make -j2` 通过。
- 复测：登录管理端打开「游戏内容管理」，确认不再出现大小限制错误页；切换多场景、编辑/保存 NPC、边缘传送点与副本向导字段仍可提交。
- 相邻页：怪物管理 Actor 选择、任务管理、商品管理仍可打开。

## 仍未知 / 风险

- 侧栏「克隆自」仍保留一份完整场景 `<select>`（仅一份，体积可接受）。
- 资源扫描上限仍为 Actor/XSE 1024、SCE 512；仓库内实体文件可能更多，目录截断是既有行为，与本次溢出无关。
- 若未来单页 NPC 卡片本身极多（接近 256 上限）且再叠加超大预览 markup，仍可能逼近 2 MiB；届时应分页或按场景懒加载表单，而不是再盲目加大缓冲。
