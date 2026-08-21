# CBE 模拟器：背包下发截断为前 64 格

日期：2026-08-20  
范围：**仅模拟器端**（`src/network-client.c` / JianghuOL JNI 同名文件）；服务端与 Pixi 仍可持有/下发至 256。

## 触发

Pixi/服侧背包容量与行数可大于 64。CBE `InitTimerControl(64, …)` 只有 64 个逻辑槽；把完整 `17/1` / `30/21` 交给 CBE 时，超出部分不被认可（闪退或列表异常）。

## 契约

| 端 | 容量 |
|----|------|
| CBE 客户 | 硬顶 **64** 逻辑槽 |
| 服务端 / Pixi | 可达 **256**（`VM_NET_MOCK_BACKPACK_CLIENT_LOGICAL_CAPACITY`） |

## 修改

在 remote 响应投递进 VM 之前，`vm_client_cbe_clamp_backpack_packet`：

1. **`1/17/1`（带 `maxnum`，即角色背包列表，不含商店 17/1）**  
   - `maxnum` 夹到 ≤64  
   - `iteminfo` 行数（u8/i16 前缀）与行体只保留前 64 行  
2. **`1/30/21`**  
   - `iteminfo` 只保留前 64 行  
   - `gridnum` 改为实际保留行数（且 ≤64）

未改服务端持久化；第 65 格及以后仍在库里，只是本 CBE 客户端本会话看不见。

## 验证

1. 编 JianghuOL `assembleDebug`，装到机/模拟器  
2. 角色背包行数 >64：登录后开背包，只见前 64 件；日志可见  
   `cbe_backpack_clamp object=17/1` / `object=30/21`  
3. 行数 ≤64：无 clamp 日志，列表与服一致  
4. 商店 `17/1`（无 `maxnum`）不受影响  
