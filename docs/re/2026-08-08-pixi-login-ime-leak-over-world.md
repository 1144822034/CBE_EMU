# 进游戏后中间残留登录输入框

日期：2026-08-08

## 触发与现象

登录成功进入选角/大地图后，屏幕中部仍可见（或可点到）账号/密码 HTML 输入框。

## 根因

`doLogin` / `onRoles` 路径：`showPhase("role")` 只把 `title.visible = false`，**没有**调用 `title.setMode(...)` 离开 login。

结果：

1. `TitleScreen.mode` 仍为 `"login"`
2. 光标定时器 `startCaret` → 每 450ms `redrawLogin()`
3. `redrawLogin` 无条件 `LoginIme.show(...)`，把透明 `<input>` 重新挂到 `#phone-wrap` 登录框坐标（约屏幕中部）
4. `dismissOverlayIme()` 虽曾 `display:none`，随后的 `ime.show` 又把它打开

首次偏离在相位切换契约：离开标题流必须停 caret / hide IME，不能只藏 Pixi 容器。

## 修改

- `TitleScreen.releaseIme()`：停 caret + `ime.hide()`，不重启 splash
- `showPhase`：非 splash/login/servers 时调用 `releaseIme()`
- `redrawLogin`：仅 `mode==="login" && visible` 时 `ime.show`
- 离开 role 时 `roles.cancelDeleteConfirm()`，避免删除确认 IME 残留
- 创建角色：`openGen` 守卫异步 `refreshPreview`；`syncNameIme` 在 `!visible` 时 hide；`releaseIme`
- 聊天：取消 deferred `focusInput` / 发送回调在关闭后的重聚焦；`releaseIme`；`showPhase` 离 chat 必释放

## 验证

1. 登录进选角/大地图：中部无输入框，点屏幕不会弹系统键盘。
2. 再退回登录页：输入框正常可用。
3. 打开创建角色后返回选角：无名字输入框残留。
4. 打开聊天后立刻返回大地图：无输入条残留、不弹系统键盘。
