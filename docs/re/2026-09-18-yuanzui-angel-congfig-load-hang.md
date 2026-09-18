# 2026-09-18 原罪天使间歇卡加载（SinAngelCongfig / title FSM state 7）

## 现象

启动器打开 `CBE/原罪天使(大型RPG)V100.CBE`：**两次里约有一次**停在加载，不再进标题。

`bin/client_out.txt` 典型卡死尾部：

1. `http_get_mock .../SinAngelCongfig ... body=1`
2. `ScreenInit Ok` 后第二次 `DF_DataPackage_DoLoading`
3. 大量 `[info][wpay_trace] game_pay_save/call`（`pc=0100271a/01002746`，`r0=7`）循环

说明：这些 PC 上的 `game_pay_*` 只是魔城（Castlevania）计费探针标签；在原罪天使 BE 镜像里同一地址是**标题状态机/输入分发**，不是支付。

## 触发

```text
cd bin
main.exe
# 启动器进入原罪天使；重复开两次对比
```

## 根因链（首次偏离 → 症状）

1. **标题状态半字**在 `R9+0x2008+2`。逻辑分发 `0x1001fb0`：`state==7` → `0x10014d8`（等 Congfig HTTP）；输入分发 `0x1002714`：`state==7` 为空操作，但每帧仍被钩到 → 日志里的 “wpay_trace” 风暴。

2. **本地 stub 契约**（`0x1000fa8` / `0x1006baa`）：
   - 打开 `SinAngel_Resource/SinAngelCongfig` **失败** → `6baa` 返回 0 → 走 CoolBar HTTP，并 `strh #7`
   - 打开成功但 `0x100691c` 校验失败（`size<=4` 或头校验不符）→ `6baa` 返回 **2** → `0fa8` **直接 `state=0`，不下载**（可进游戏）
   - 校验通过 → 返回 1 → 同样 `state=0`

3. **首次偏离**：cwd 找不到 `SinAngel_Resource/...`（有时在 `bin/`，有时在仓库根）→ HTTP；`http_get_mock` 默认 `body={1}` 被当成“下载成功”。

4. **`0x100691c` 需要 `size>4` + 与全局累加校验一致的头字段**。`{1}` / 仓库里 1 字节 stub **不能**通过校验 → `14d8` 再次 `strh #7` → **永久停在 state 7**（卡加载）。

5. 间歇性：某次若 cwd 已能打开无效 stub → `6baa==2` → `state=0` → 正常；另一次走 HTTP 成功假体 → 卡死。

## 修改

| 位置 | 改动 |
|------|------|
| `src/vmFunc.c` `vm_file_try_resolve_sinangel_resource_path` | 解析 `bin/` / `../bin/`；仍没有则写入 1 字节 stub |
| `src/main.c` `vm_net_queue_http_get_mock_response` | `SinAngelCongfig` 改为 `events=2`（失败），禁止再回 `body=1` 成功 |

权威完整 Congfig 包仍 **unresolved**（死链 CDN）；离线路径以“无效 stub → 跳过下载”为准，与客端 `6baa==2` 分支一致。

## 验证

```text
cd bin
set CBE_LOG_MODE=debug
main.exe --cbe="CBE/原罪天使(大型RPG)V100.CBE" --no-launcher
```

期望：

- 优先出现本地打开 / `seeded offline SinAngelCongfig`，**不要**再 `http_get_mock ... SinAngelCongfig ... body=1`
- 若仍请求 HTTP：日志为 `events=2`，且不再无限 `wpay_trace game_pay_*`
- 连续打开两次都能过加载进标题
