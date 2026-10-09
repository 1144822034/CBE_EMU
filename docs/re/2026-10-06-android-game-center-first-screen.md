# Android 经典怀旧：刚进入应先显示游戏中心

日期: 2026-10-06

## 预期

与 PC 一致：进入经典怀旧后先显示「游戏中心」列表，轻触选择后再加载 CBE。

## 根因 A（代码门控）

`cbeInit` 在 `CBE_PLATFORM_ANDROID` 下曾跳过 `vm_game_launcher_init`，直接加载默认 CBE。

## 根因 B（打包“看似无效”，实为热更踩库）— 2026-10-06 实测

设备 `filesDir/jni-hot/files/arm64-v8a/libmtksim.so` = **657408** 字节（远程旧库），
APK 内 `lib/arm64-v8a/libmtksim.so` = **662176** 字节（含游戏中心）。
`CbeNativeLoader` 优先 `System.load(overlay)`，冷启动/`CbeEmu.launch` 的
`CbeJniHotUpdate.sync` 在 remote≠apk 时把**旧远程**再拉回 overlay → 新 APK 永远不生效。

这与「某次对话打包正常、后续不正常」吻合：后续启用了 jni-hot，远程清单未跟上新 so。

## 修改

- `CBE_HAS_GAME_LAUNCHER`：Android 启用游戏中心（轻触进入）
- APK 升级 wipe 后设 `trust_apk_until_remote_matches`：remote≠apk 时**不**装旧 overlay
- `CbeNativeLoader`：trust_apk 时强制加载 APK 库
- 本地 `dist/jni-hot/` 已更新；需把新 manifest/so 发布到热更源后才恢复热更覆盖

## 验证

- 覆盖安装 v88 → wipe jni-hot → log `loaded apk jniLibs`
- 经典怀旧首屏：「游戏中心」+「轻触选择游戏」

## 2026-10-09 再丢失与恢复

`main.c` 同步后再次丢掉 Android 游戏中心门控（`CBE_HAS_GAME_LAUNCHER` 对 Android 关闭、
`cbeInit` 直接加载默认 CBE、`cbeRun` 不等待 `g_cbeVmReadyForRun`）。已按
`tmp_jni_main_c_before_sync.c` 契约恢复：Android 也定义 `CBE_HAS_GAME_LAUNCHER`，
`cbeInit` 早退进游戏中心，`load_and_start` / 直载路径置 `g_cbeVmReadyForRun`，
`cbeRun` 等待就绪后再 `RunArmProgram`。

覆盖安装 **v94**（`versionCode 94`）→ wipe jni-hot → log
`game center initialized … (android)` + `loaded apk jniLibs`。
