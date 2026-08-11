# 场景战斗管理 Actor 预览选择器

## 问题与边界

场景战斗怪的本体 Actor 与 SCE2 kind-3 的 field18 效果 Actor 原先是普通下拉框。
动态 NPC 已有带缩略图的 Actor 选择器，但它刻意排除不兼容动态 NPC 生命周期的资源，
不能直接复用到场景战斗怪：合法的效果资源（例如 `e_ghostfireR.actor`）不应因此被
过滤。

## 修正

- 场景战斗怪的 `actor_resource` 与 `effect_resource` 都改为隐藏原有 `<select>`
  加“搜索与预览”按钮；表单字段名称和服务端保存协议不变。
- 场景战斗页加载既有 `admin.js` 预览交互，选择弹窗显示服务端资源目录中的全部
  `.actor` 文件及由 `actor-preview.svg` 生成的缩略图。
- 动态 NPC 继续使用其受限资源目录；两种 Actor 选择器各自生成目录，避免把动态
  NPC 的兼容规则误用于战斗怪或战斗效果。
- 最终保存仍由场景战斗怪服务端校验场景、坐标、文件扩展名与服务端资源存在性。

## 验证

- `admin.js` 通过 Node.js 语法检查。
- `git diff --check -- src/web_admin_server.c`。
- `make -j2`。
