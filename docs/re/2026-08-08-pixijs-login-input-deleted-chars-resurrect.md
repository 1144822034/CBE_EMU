# PixiJS 登录框：删除后再输入，已删字符复现

## 触发

账号登录页点输入框（`LoginIme` 透明 `<input>` 聚焦）后删除部分字符，再继续输入，已删除内容会重新出现。

## 根因

两条写入路径不同步：

1. `main.ts` 全局 `keydown` → `TitleScreen.typeKey` 改 `userValue`/`passValue` 并 `preventDefault`。
2. `LoginIme.setValues` 在字段聚焦时**故意不写回** HTML，避免打断光标；因此 `<input>.value` 仍是删除前的串。
3. 软键盘/后续 `input` 事件以 HTML 旧值为底再插入新字符 → `onChange` 把含已删字符的串写回画布状态。

首个偏离：`typeKey` 在 overlay 聚焦时仍拦截 Backspace/字符，却不同步 HTML。

## 修复

- Overlay 聚焦时 `typeKey` 放行，由原生 `<input>` 独占编辑（与 `ChatIme.isFocused` 同一契约）。
- 非 IME 键盘编辑与粘贴走 `LoginIme.forceValues`，强制对齐双缓冲。

## 验证

- 点账号/密码框：连续退格再输入，不应带回已删字符。
- 未点框、仅用物理键盘输入/退格仍可用。
- Ctrl+V 粘贴后画布与后续继续输入一致。
