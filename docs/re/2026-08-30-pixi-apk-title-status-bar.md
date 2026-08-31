# APK 标题页露出系统状态栏 / 右上角白点（2026-08-30）

## 现象

未进游戏的标题菜单（开始游戏 / 登陆游戏…）顶部像多了一条状态栏；截图右上角常见白色圆点。

## 根因

两处叠在一起：

1. **系统状态栏**：`AppTheme.NoActionBar` 未设 fullscreen；`MainActivity` 也未进沉浸式。Capacitor WebView 默认留出状态栏，OEM 上标题页就能看见时间/电量条或顶条留白（`#app` 还有 `env(safe-area-inset-top)`）。
2. **HTML 白点**：`#touch-chrome-toggle`（隐藏/显示方向键）固定在 `#phone-wrap` 右上。标题 `Background.gif` 顶部本就是黑边，「永久免费」旁的白点很像系统状态指示。

## 修改

| 层 | 行为 |
|----|------|
| `styles.xml` | `windowFullscreen` + 透明 statusBar |
| `MainActivity` | immersive sticky（hide status+nav）；`onResume` / `onWindowFocusChanged` 再藏 |
| `main.ts` `showPhase` | splash/login/register/servers/role/create-role 隐藏白点开关 |

进图后白点恢复，便于收起 D-pad。

## 浏览器

系统状态栏由浏览器/OS 托管，网页**不能**像 APK 那样强制藏掉（除非用户手势进 Fullscreen / PWA standalone）。

标题页「修/拍/…」误显见 `2026-08-30-pixi-title-status-strip-leak.md`（GPU resume 泄漏），与白点无关。

## 验证

1. 冷启 APK 停在标题菜单：无系统状态栏图标/顶条。
2. 进世界：系统栏仍隐藏（APK）；右上白点可切换方向键。
3. 下拉通知后回游戏：系统栏应再次自动藏起。
