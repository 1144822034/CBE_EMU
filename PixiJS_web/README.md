# PixiJS 江湖

独立 Web 客户端：用 **PixiJS** 复刻 CBE **240×400** 界面与软键交互，资源与协议来自仓库 `JHOnlineData` / mock-server。玩法权威仍在 C 服务端。

## 已实现

- **CBE LCD 壳**（240×400）+ 底栏软键（菜单 / 确定 / 返回）
- 标题、选角、主界面 HUD、菜单弹层、战斗层（全在屏内，非网页表单）
- `title.gif` / `ui_selectchar.gif` 等原版 UI 图
- WT 登录进图、心跳、NPC/玩家、开战（**仅 Live**，默认 gateway `ws://47.108.210.191:19092`）

## 操作

| 键 / 触控 | 作用 |
|-----------|------|
| ↑↓ / W S / 底栏方向键 | 列表 · 走动 |
| Q / 左软 | 左软键 |
| Enter / 中软 | 中软键 |
| E / Esc / 右软 | 右软键 |
| WASD（进图后） | 走动 |

手机全屏：去掉网页外壳，画面按 240×400 铺满上半区，底部为方向键与左右软键。

```bash
cd PixiJS_web
npm install && npm run gen:scenes && npm run dev
```

实网：连固定 gateway（或 `?gateway=` / `localStorage.pix_gateway` 调试覆盖）。登录 `codeVersion` 读 `client_code_version.txt`，须与服端 `server_code_version.txt` 一致。

### Android APK（离线 JHOnlineData）

```bash
cd PixiJS_web
npm run build          # sync web/fs/JHOnlineData → public → dist
npx cap sync android
# JDK 21 + 本机已缓存的 Gradle 8.11.1：
set JAVA_HOME=C:\Users\Administrator\.gradle\jdks\eclipse_adoptium-21-amd64-windows.2
set ANDROID_HOME=D:\installsoft\JDK\android-sdk
"%USERPROFILE%\.gradle\wrapper\dists\gradle-8.11.1-all\6gcpoccneql1b0krsle0llw37\gradle-8.11.1\bin\gradle.bat" -p android assembleDebug
```

APK：`android/app/build/outputs/apk/debug/app-debug.apk`

息屏保活：`MainActivity` 持有 `PARTIAL_WAKE_LOCK`，暂停时仍 `resumeTimers`；首次可弹「忽略电池优化」。亮屏后前端会 `wakeFromBackground` 重连网关。

### 手机 / 局域网调试（浏览器）

Vite 监听 `0.0.0.0` 时可在同 Wi‑Fi 打开 `http://<pc-ip>:5173`；gateway 仍默认公网 IP，可用 `?gateway=` 覆盖。

```bash
npm run smoke          # 离线 decode / scene / protocol / actor
npm run smoke:live     # 经 gateway 打 1/12 取服列表
npm run gateway        # Node 版 WS↔TCP 网关（:19092 → :19090）
npm run gateway:exe    # 打包 gateway-release/ws-gateway.exe（勿放 dist，避免 Android 出包清掉）
npm run build:exe      # 竖屏 Electron 便携包 → electron-release/江湖OL-*-portable.exe
npm run electron       # 已有 dist/ 时本地打开竖屏窗口
```

取证短记：`docs/re/2026-08-06-pixijs-live-core-loop.md`、`docs/re/2026-08-08-pixijs-capacitor-apk-offline-assets.md`、`docs/re/2026-08-08-pixijs-ws-gateway-exe.md`、`docs/re/2026-08-11-pixijs-electron-portrait-exe.md`。
