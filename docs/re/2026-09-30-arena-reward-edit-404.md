# 比武排行奖励「编辑」提示页面不存在

日期：2026-09-30

## 症状

管理后台「比武 → 排行奖励」点「编辑」后浏览器显示 `页面不存在。`（HTTP 404）。

## 触发条件

1. 打开 `/?tab=arena`（经前缀后为 `/admin-418yz6/?tab=arena`）。
2. 排行奖励表中任一行点「编辑」。

## 契约与第一次偏离

- **契约**：编辑链接经 `vm_mock_admin_prefix_page_routes` 后应为
  `/admin-418yz6/?tab=arena&edit_job=…&edit_bracket=…&edit_rank=…`，
  与天梯里程碑编辑一致，命中 `strcmp(target, VM_MOCK_ADMIN_ROOT_PATH)==0` 分支并渲染编辑表单。
- **第一次偏离**：源 HTML 已写死 `href="/admin-418yz6/?tab=arena…"`，前缀重写再匹配 `href="/`，
  产出 `/admin-418yz6/admin-418yz6/?tab=arena…`；路由只认精确 root，落到末尾 404。

## 根因

`prefix_page_routes` 约定页面内链写成 `href="/?tab=…"` / `action="/action"`，由它统一加 `VM_MOCK_ADMIN_ROOT_PATH`。  
奖励编辑/取消链接误嵌入完整 `VM_MOCK_ADMIN_ROOT_PATH`，导致双前缀。  
（HTTP `Location` 重定向仍应写完整路径，不经该重写。）

## 修改点

- `src/web_admin_arena.inc.c`：编辑、取消链接改为 `/?tab=arena…`（对齐 `web_admin_ladder.inc.c`）。

## 验证

- `make -j2`。
- 复测：比武页点「编辑」进入带只读职业/段位/名次的表单；「取消编辑」回到空表单；保存奖励仍正常重定向。

## 仍未知 / 风险

- 其它页若在 HTML 里写死 `href="…VM_MOCK_ADMIN_ROOT_PATH…"` 会同类双前缀；当前检索仅比武奖励编辑命中此模式。
