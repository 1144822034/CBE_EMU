# 挂机中再点挂机停止

Date: 2026-07-28

Status: superseded — 见 `2026-07-28-hangup-button-delay-stop-after.md`

旧实现：第二次挂机按钮立即清 prefer + 软 ACK / 拆场。软取消会卡「获取数据」。

现行契约：

- 未挂机：提示「5秒后开始挂机」，poll 延迟开战
- 已挂机：提示「下一场完成后挂机停止」，`StopAfterBattle`；地图侧 fallthrough
  最后一场以满足 Type=2 进场；结束后 `4/8` 拆场并提示「已停止挂机」
