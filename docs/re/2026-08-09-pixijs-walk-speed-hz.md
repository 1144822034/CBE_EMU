# PixiJS APK：部分机型走路过快（2026-08-09）

## 现象

发布 APK 后，部分玩家设备上人物移动明显快于其他机 / 快于网页调试体感。

## 根因

`SceneView.tick()` 本地移动用固定 `speed = 1.0` 像素/次 ticker 回调，未乘 `dt`。  
高刷新率（90/120Hz）或系统让 WebView 更频繁回调 → 单位时间位移更大。  
同文件里走路动画、`ActorLayer` 队友/巡逻已按时间推进；仅本地位移漏了。

## 修改

`PixiJS_web/src/render/SceneView.ts`：`WALK_PX_PER_MS = 1/(1000/60)`（对齐原 ~60Hz 手感），`speed = WALK_PX_PER_MS * clamp(dt,0..50)`。  
`movePixelAcc` / `onMoveDir` 仍按实际走过像素累计，网速步频随之归一。

## 验证

1. 60Hz 与 120Hz（或 Chrome 限制帧率）同场景按住方向：路程/秒应接近。
2. 碰撞滑墙、传送门触发、移动上报仍正常。
