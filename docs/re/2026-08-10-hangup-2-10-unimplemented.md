# 2026-08-10：点挂机仍提示「功能暂未实现(1/2/10)」

phase: hangup-2-10-unimplemented  
status: fixed

## 现象

Pixi 点挂机后聊天出现系统提示 `功能暂未实现(1/2/10)`（与战斗心得伴生 `2/10` 的 tip 文案相同，但是挂机按钮路径）。

## 根因

挂机契约是 `1/2/10 Type=2 + 空 1/25/3`（可选 trailing `2/1` moveinfo）。派发座位偏晚；解析对伴生 `25/5` keepalive 过严时整包失配；或解析命中后 builder 中途 `return 0`，最终落到 `unimplemented-handler-notice`，tip 取首对象 → `(1/2/10)`。

客户端 `buildHangupStartRequest()` 线序正确（24 字节 Type=2+25/3），问题在服务端归属与失败兜底。

## 修改

| 文件 | 改动 |
|------|------|
| `mock_server_battle.c` | 放宽解析（Type/type、可无 25/3、忽略空 25/5）；owned soft-fail：`2/10+25/12+1/3/3`「挂机失败…」 |
| `mock_server_dispatch.c` | 挂机提前到 moveinfo 之后；builder=0 时 soft-fail，禁止 unimplemented tip |

## 验证

1. 野外点挂机：应见「已开始挂机」或正常开战，**无**「功能暂未实现(1/2/10)」、**无**「挂机失败」
2. 城内点挂机：应见「当前场景不能挂机」，不是 unimplemented tip
3. 吃过战斗心得（828）后挂机：三怪 subtype-10 仍应开战
4. 日志：`builtin-hangup-battle-start`（或场景拒绝），不是 `…-soft-fail` / `unimplemented-handler-notice`

## 续：挂机失败 soft-fail（同日）

soft-fail 证明解析已命中，builder 返回 0。生产日志模式吞掉 `[info]`，表面上看像“直接失败”。

根因候选（已修）：`battleInfo[160]` 在战斗心得强制 `enemies=3`（或队长组队 blob）时放不下 subtype-10 多怪+名字，`build_battle_start_info_blob` 返回 0。已扩到 `512`，并为 builder 失败点补 `[error]` 取证；settle 前置 4/8 失败时清掉 settle 锁，避免永久 soft-fail。
