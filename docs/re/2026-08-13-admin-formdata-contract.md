# 后台异步表单编码契约

## 触发条件

后台管理页的怪物、任务与 NPC 编辑器使用 `fetch(new FormData(form))`
异步提交。浏览器会将该请求编码为 `multipart/form-data`。部分页面的
普通 HTML 提交则使用 `application/x-www-form-urlencoded`。

## 首次偏离与根因

服务端的 `vm_mock_admin_form_value()` 以前只扫描 `key=value&...` 形式。
收到 multipart 请求时，隐藏字段 `action` 无法被解析；
`vm_mock_admin_handle_action()` 因而走“请求参数不完整”的默认重定向，
目标没有携带 `tab=content`，页面便显示为账号管理。

这不是 NPC 数据本身缺少字段，也不应由各编辑器分别转换请求格式。

## 修正

公共表单读取层现在根据请求体首个 multipart 边界读取文本字段，同时
保留 URL 编码的既有行为和重复字段（`<select multiple>`）支持。
文件上传仍由其独立的、长度感知的 multipart 解析器处理。

## 验证边界

`make -j2` 编译通过。应手动覆盖 NPC 新增/保存、怪物保存/掉落编辑、
任务保存/奖励编辑及 NPC 库存批量编辑，确认均保留各自的原页面与局部
刷新行为。
