# Pixi 内嵌 CBE：确认「经典怀旧」后整 App 闪退

日期: 2026-10-05

## 触发

标题菜单「经典怀旧」→ 确认 → `CbeEmu.launch` 打开 `mt6252simulator.MainActivity` → 约 150ms 后进程退出。

## 证据

`adb logcat -b crash`（`com.jianghu.pixijs` v80，多次复现）:

```
java.lang.UnsatisfiedLinkError: No implementation found for int
com.xiaoxiao.mt6252simulator.MainActivity.nativeConsumeNeedLoginTip()
  at MainActivity.pollNeedLoginTip(MainActivity.java:1283)
  at MainActivity$4.run (textInputPollRunnable)
```

`nm -D` 包内 `libmtksim.so`：有 `nativeConsumeAccountWebOpenRequest`，**没有** `nativeConsumeNeedLoginTip`。源码 `cbe_jni.c` 已导出该符号，预编译 `.so` 未跟上。

## 根因

Java 活动从较新的 JianghuOL 拷来，`StartRun` → `startTextInputPolling` 每 150ms 调 `nativeConsumeNeedLoginTip`。APK `jniLibs` 仍是旧 `libmtksim.so`。符号缺失抛 `UnsatisfiedLinkError`，未捕获则主线程 FATAL，Pixi 与 CBE 同进程一起死。

第一次偏离：轮询调用了当前映射库中不存在的 JNI，不是 `mtk_open`/`startEmu` 失败。

## 修改

- `pollNeedLoginTip`：捕获 `UnsatisfiedLinkError`，记日志后停用该轮询（旧库行为：不弹「请先登录游戏」）。
- `GameKeepAliveService`：API 34+ `startForeground` 带 `DATA_SYNC`（targetSdk 35，避免过了 JNI 闪退后撞上缺 FGS type）。

后续仍应 `ndk-build` 当前 `cbe_jni.c` 替换 `JianghuOL/app/src/main/jni/libs/*/libmtksim.so`（`gameLauncher.c` 在 Android 路径需走 `android_compat.h`，直接 ndk-build 会缺 SDL.h）。

## 验证

覆盖安装后：确认进经典怀旧不再 FATAL；logcat 可有一次 `missing nativeConsumeNeedLoginTip`。
