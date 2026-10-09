# Android：`empty.sce` 在「获取版本信息」后 Cpu 异常

日期: 2026-10-09

## 与历史文档关系

- [`2026-10-06-android-vmassert-picturelibrary-bare-gif.md`](2026-10-06-android-vmassert-picturelibrary-bare-gif.md) 已记：标题 GIF 过了之后 guest 要 `empty.sce`，包/磁盘都没有，标 **unresolved**。
- [`2026-10-06-android-outdoor-black-tiles-gifdecode.md`](2026-10-06-android-outdoor-black-tiles-gifdecode.md) 续记：GIF `cacheId=0x7000+` 后 Cpu 落在 `empty.sce`；当时补的是 **dist-hot / gifDecode 堆**，不是 empty.sce 本体。
- 梦境/出店文档（`stream_data_result … caller=0100622e result=0` → `SCR_Render`）证明：跳过或半开 `DoLoading` 会走同一 UC_ERR_MAP 族。见 [`2026-08-02-dream-reenter-518c-screeninit.md`](2026-08-02-dream-reenter-518c-screeninit.md)、[`2026-10-05-shop-return-same-scene-30-2-hold.md`](2026-10-05-shop-return-same-scene-30-2-hold.md)。

## 新证据（v94 / 游戏中心已恢复）

1. `empty.sce` / `empty.map` 是 **`江湖OL.cbe` 内字面量**（offset `0xe18a` 一带），不是漏拷的 JHOnlineData 文件。PC `bin/JHOnlineData` 也没有该文件。
2. PC `client_out` 同路径：`1/18/9` → `resp=63` → `ScreenResume` → push `0105a814` → `DoLoading package=010534ec` → push `010534b4` → `SCR_FUNC init=5025b39` → `ScreenInit Ok`。**从不** `GetFileID empty.sce`。
3. Android 同序也调用了 `DoLoading package=010534ec source=0105a800 mode=1`，但随后：
   - `GetFileID miss name='empty.sce' cacheId=ffffffff`
   - `stream_data_result manager=game_lcd caller=0100622e sce_ctx=00000000 … result=0`
   - `SCR_Render` / `UC_ERR_MAP` → Java「当前Cpu状态异常」
4. 因此第一次偏离 **不是** 缺 `empty.sce` 文件，也不是 JHOnlineData 解析失效（标题 GIF 已是 `0x7000+`）。偏离在：**登录壳 `010534b4` 进 ScreenInit 前/中，Android 走了 `GetStreamDataFormRes(empty.sce)` 且 `sce_ctx=0`，PC 则直接 `init=5025b39` 成功。**

## 根因陈述（待钉死）

- 触发：版本握手 `1/18/9` 回包后打开标题登录壳。
- 被违反契约：登录壳 ScreenInit 需要有效 DF/场景流；空 `sce_ctx` + 占位名 `empty.sce` 取流失败不得继续 Render。
- 首个错误状态：`stream_data_result … result=0`（caller `0100622e`）。
- 仍未知：为何同 CBE/同 CBM hash 下 Android 会带着 `empty.sce` 进 `GetStreamDataFormRes`，而 PC 不会。禁止用假 `empty.sce` 文件或吞 `UC_ERR_MAP` 当完成。

## 修复（2026-10-09）

CBE 标题资源名表（约 `0x5afec`）在 `loading.gif`/`UI*.gif` 旁列出
`empty.sce`/`empty.map`。Android 在 `1/18/9` 后进登录壳时会
`GetFileID(empty.sce)` → `GetStreamDataFormRes(res=0)` → 读空指针 →
`UC_ERR_MAP` / Cpu 弹窗。磁盘与 PC 均无该文件。

修改（`src/vmFunc.c` + JNI 副本）：

1. 包 miss 且名为 `empty.sce`/`empty.map`：记入资源 cache，
   `GetFileByID` 合成 **0xf1** 伪流（最小 SCE2 / 空 map 体）。
2. `GetStreamDataFormRes(a1==0)` 直接回 0，避免再踩空映射。

验证：log 见 `empty placeholder name='empty.sce'` 与 `cacheId=0x7000+`，
随后 `SCR_FUNC init=5025b39` / `ScreenInit Ok`，无 Cpu 弹窗。
