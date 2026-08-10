# 属性页血蓝双计：displayMax 改裸池（不发客户端）

日期：2026-08-05

## 现象

蔡文姬面板 `16608/6006`，战斗 `11553/4153`。服务端 overlay 已是 `11553/4153`。

## 根因

客户端 wear-apply 会再加装备气血/法力。actorinfo 的 `primaryDisplayMax` /
`secondaryDisplayMax` 原先发**已含装备的全额**，属性页变成 `全额+装备`。

战斗 `battleinfo` 一直用 `build_player_stats` 全额，两边分叉。

## 不发 CBE 的修法

1. **服务端**（真机/模拟器都生效）：`displayMax` 与 `baseMax` 同发**裸池**；
   当前血蓝仍按全额池夹紧。客户端叠装后应落到与战斗一致的全额。
2. **Host**（仅模拟器）：`vm_host_realign_attr_panel_value_rows` 用 overlay
   的 `maxHp/maxMp` 纠正属性页第 6/7 行的**最大**半段，格式保持客户端的
   `当前/最大`（勿写成单个最大值，否则 PC 会丢掉当前血）。
   - PC：`src/main.c` → `vm_host_write_attr_panel_vital_row`
   - Android JNI：同路径；无 overlay 文件时不改写，仍靠 displayMax 裸池

真机无 PC overlay 文件时，仍靠上面的 **displayMax 裸池** 纠正双计；host 6/7
仅在能读到 overlay 时补写最大半段。

不改 JianghuOL.CBE，不发新客户端包（需重编安卓 so/APK 才带上 JNI host）。

## 验证

1. `make -j2`（server + 若改了 main 的 client/host 目标）
2. 安卓：重编 JNI / 安装 APK 后重登
3. 重登蔡文姬：属性页生命/法力应 ≈ `当前/11553`、`当前/4153`（带斜杠），与进战上限一致
4. 穿脱气血装：面板与下一场战斗同向变化
