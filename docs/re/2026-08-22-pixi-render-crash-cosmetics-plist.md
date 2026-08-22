# Pixi RENDER_CRASH：妆容 plist idle unload / loadGen 竞态

## 症状

LCD 黑或冻帧，HTML 方向键仍在但无反馈；动画停。`lost=false`，有时 `blank=false`（冻住上一帧）。

取证：`[gl] forensic RENDER_CRASH why=render-crash:window-error`，堆栈 `getAdjustedBlendModeBlend` → `alphaMode` of null。紧邻大量 `[actors] cosmetics paint`（多 peer 翅/骑）。

## 根因

1. **idle `source.unload` 后 atlas `cache` 仍命中**：浏览器 retain=0 后约 2.5s 卸 GPU；再 paint 复用死 Texture → Batcher 读 null `_source.alphaMode`。
2. **`setWingId` / mount / chenghao 的 loadGen 竞态**：`await` 后只校验一次 gen，随后 `clear()` 可把 retain 打到 0 并毁掉 sprite，旧协程仍把贴图挂回舞台。

崩溃 PC 是症状；第一次偏离在妆容贴图生命周期契约。

## 修改

- `plistAtlas`：unload 时按 pngUrl **剔 cache**；`retain` 时 `ensurePlistSourceAlive`；`bindPlistAtlasIfCurrent`。
- `wingFx` / `mountFx` / `chenghaoFx`：rebind / 挂 sprite 后再验 gen；过期只 `return`（勿 `clear()`，否则会顶掉更新的加载）。
- `main`：`alphaMode` 类 render 错误**吞掉该帧**并打 `RENDER_CRASH`（防 ticker 整屏停死）。

**回归（已修）**：
1. 过期协程挂 Sprite 后若 `clear()`，会把 `loadGen` 再 +1，把正在进行的新加载也作废。
2. peer 每 ~500ms `refreshPeerHeavyCosmetics` 在 `atlas==null` 时仍 `++loadGen`，永久取消加载 → 只有自己有妆容。同 id 改为不重开加载；称号同 key 不拆 badge。
3. 坐骑 `setFacing` 在 atlas 未就绪时反复 `reloadSheet`（peer 每步移动）→ 再修 coalesce + `loadingKey`。
4. 浏览器关闭 idle `source.unload`（避免死贴图）；调试：`window.__pixiDebugPeerCosmetics()`。
5. **卡死**：同 id 短路在「wingId 已写、atlas 仍空、加载已取消」时永不重试 → 无 `[wing] ready`。改为 `loadInFlight`：仅加载中跳过，失败/取消后可重试。
## 验证

1. 城镇多人妆容 + 开系统面板久挂，不应再 Uncaught `alphaMode`；偶发应见 `swallow render alphaMode` 后继续动。
2. 切场景 / peer 进出后翅骑仍显示；**附近玩家翅/骑/称号应与本人一样可见**（菜单未关「附近特效」）。
3. 仍出现时导出 `__pixiDumpForensic()`，看 `brokenTex`。
