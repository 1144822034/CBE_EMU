# PixiJS 安卓内嵌 CBE + JNI 热更新

日期: 2026-10-05

## 目标

同一 APK：PixiJS Capacitor WebView 为主界面；开始界面「经典怀旧」打开现有 JNI 模拟器（`libmtksim` + `libunicorn`）。`.so` 可按 dist 热更同源地址差量更新。

## 嵌入

- 活动：`com.xiaoxiao.mt6252simulator.MainActivity`（JNI 符号未改），布局 `activity_cbe_emu`。
- 运行目录：`filesDir/JHOnline`（不申请管理所有文件）。`JHOnlineData` 从 Capacitor `assets/public/JHOnlineData` 解压；`江湖OL.cbe` / `font_gb.uc3` / `servers.conf` 由 Gradle `syncCbeRuntimeAssets` 从 `JianghuOL/app/src/main/assets` 拷进中间产物。
- CBE 按名取图/文件时共用 Pixi 的 `JHOnlineData`：先 `filesDir/dist-hot/files/JHOnlineData/`，再 cwd 下 `JHOnlineData/`。不另拷一份资源树。
- 基线 `.so`：`jniLibs` 指向 `JianghuOL/app/src/main/jni/libs`（arm64-v8a / x86_64）。
- 保活：CBE 仍用 `GameKeepAliveService`；Pixi 息屏 FGS 互不影响。

## JNI 热更新

- 清单：`http://106.54.59.92:8081/jni-hot/jni-hot-manifest.json`
- 文件：`jni-hot/<abi>/libunicorn.so`、`libmtksim.so`（sha256）；另有 `jni-hot/ui/cbe_dimens.xml`（方向键尺寸覆盖，不替换 APK 编译布局）
- 实现：`CbeJniHotUpdate` → `filesDir/jni-hot/files/`；`CbeNativeLoader` 在首次 `System.load` 前加载覆盖层 `libmtksim.so`（`libunicorn.so` 可与 APK 混用）。`CbeUiOverlay` 在打开经典怀旧 inflate 后读取 `ui/cbe_dimens.xml` 套到方向键。冷启动 `DistHotUpdate.start` 也会拉 JNI。
- UI xml 与 `.so` 的 prefer-APK 无关；旧 APK 没有 `CbeUiOverlay` 时下载了 xml 也不会生效，需先装一带应用逻辑的包。
- 2026-10-05 取证：线上清单与手机 `applied.json` 已是新 `libmtksim`（`49c89df2…`），但 unicorn 与 APK 同 hash 未写入 overlay；旧加载器要求两个 `.so` 都在 overlay 才 `System.load`，否则整组回退 APK 旧库，表现为「热更已上传但不生效」。
- 已 load 的进程不能换库；新 `.so` 下次冷启动生效。APK `versionCode` 变化时与 dist-hot 一起 wipe。
- 发布：`vite build` 写 `dist/jni-hot/`。把该目录与 dist 一并放到热更根。失败则用包内库。

## 入口

开始界面菜单「经典怀旧」。安卓：`CbeEmu.launch`。PC Electron：独立进程 `bin/main.exe` / 便携包 `resources/cbe`。同账号勿与 Pixi 同时在线。

## 未做

- 把 CBE 画进 Electron 同一窗口（PC 为独立 `main.exe` 窗口）
- 热更 CBE 资源树（安卓 dist-hot 仍不管整棵 JHOnlineData；JNI 热更除 `.so` 外只覆盖 `ui/*.xml` 尺寸）
