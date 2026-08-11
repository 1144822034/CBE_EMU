# 场景战斗怪更新后 action13 未推进

Date: 2026-08-10

Status: investigation in progress; resource-container defect corrected, entity callback contract unresolved

## 1. 当前卡点

- 可见现象：客户端完整重启并进入 00蓬莱仙岛_02.sce 后，选择小猴子的“挑战”
  选项没有进入战斗。
- 触发方式：场景战斗怪已部署，客户端接收场景内容更新并加载同名 SCE，然后从
  小猴子 NPC 对话选择 action13。
- 本轮最小目标：让客户端将部署后的战斗怪作为真实场景节点创建，使 action13
  上行携带非零 live index，随后由既有 4/5 场景战斗分支自然进入战斗。

## 2. 运行时证据

最新 bin/server_out.txt 中的顺序是：

    mock_content_update_legacy_migration scenes=1
    mock_update_version ... content=1/200/1
    mock_content_update_chunk ... files=1
    mock_update_chunk subtype=7 file=00蓬莱仙岛_02.sce ... total=403
    mock_update_chunk_complete ... action=client-install-callback
    mock_npc_dialog actor=20021 ... direct_challenge=1
    mock_direct_scene_challenge_reject ... expected_enemy=1000 requested_enemy=1000 req_index=0 expected_index=0 ... reason=live-node-unready

因此 WT 18/9 -> 18/8 -> 18/7 已实际发生，当前卡点不是缓存未失效。首次偏离是
客户端的 action13 请求没有在 25 个 scene-node 行中找到 id=1000，故
SendNPCInteractReq 写入 index=0。

只读检查也确认 bin/JHOnlineData/00蓬莱仙岛_02.sce 和服务端同名文件均为
403 字节，且解压后包含 field14=1000、名称“小猴子”和 e_monkey.actor。

## 3. IDA 目标

| binary | function/address | reason | findings |
| --- | --- | --- | --- |
| 江湖OL.CBE | SendNPCInteractReq 0x01037ED4 | action13 上行 | 逐行扫描 25 个 340 字节节点，只有 active 节点的 node+100 等于 action value 时才写入非零 index；posx/posy 固定为零。 |
| 江湖OL.CBE | task_hall_activate_selected_entry 0x010492B0 | action13 调用者 | case 13 将所选项 value 原样传给 SendNPCInteractReq。 |
| mmBattleMstarWqvga.cbm | HandleBattleStartMsg 0x000066CC | 4/5 前置 | subtype 5 用 index 与 SCE 静态 x/y 查 active kind-2 节点；缺失 live 节点不能用服务器离线 index 代替。 |

## 4. 静态资源证据

扫描服务端所有原生 SCE2 资源得到 128 个 kind=3 战斗记录样本。它们都遵循：

    u16 3, u16 x, u16 y,
    u16 5-or-6, u16 1, u16 14, u16 monster_id,
    u16 3, u16 15, u8 name_length, name,
    u16 1, u16 16, u16 visual_hint,
    u16 3, u16 17, u8 actor_length, actor_resource,
    u16 3, u16 3, u8 effect_length, effect_resource

例如 01桃花岛_01.sce 的 type-3 怪物带有 e_mucusP.actor 和
e_ghostfireR.actor。旧部署器最初只写到 field17，之后虽补入 effect Actor，却误写成
普通的 `3,18` 字符串字段，仍偏离原生 `3,3,len` 尾部信封。服务器自己的宽松验证器也
接受了该错误资源；客户端的原生场景解析器则不能将其建立为可挑战节点。

## 5. 本轮发现：外层资源容器是更早的偏离

本轮重新操作确认客户端确实安装了发布文件，但服务端写出的文件为：

```text
<u32 outer_len> [type=1] [LZSS compressed_len] [LZSS decoded_len] [LZSS tokens...]
```

`type=1` 的正式含义是未压缩字节，客户端会将其后内容直接交给场景加载器；因此该加载器
看到的是 LZSS 头，而不是 `SCE2`。原生 `.sce` 使用 `type=2` 承载同一套 LZSS 头和 token。

此前服务端的资源读取器错误地把 type 1 和 type 2 都当作 LZSS，造成两层假象：部署自检、
怪物目录和服务端日志都能看到小猴子记录，但真实客户端不会创建对应场景节点，action13
只能上行 `index=0`。这正是本次失败的第一个偏离点。

部署器现改为生成 `type=2`，并在原子写入前按客户端同一规则将最终原始资源重新解码，
要求结果逐字节等于将要发布的 SCE2 payload 且从 `SCE2` 开始。读取器也改正 type 1 的
语义，故此前错误发布的 type-1 文件会被判为未部署，必须显式重新部署，不能继续被误认为
有效。

## 6. 已修正的早期契约与当前首个偏离

场景战斗怪部署器先前产生了错误的 type-1/LZSS 外层资源；该问题已由 type-2
容器与逐字节 round-trip 检查修正。最新实际复测中，客户端已请求、下载并安装
426-byte type-2 资源，服务端与客户端安装目录的 SHA-256 一致，解压结果从 `SCE2`
开始，并包含原生字节序列 `short_spawn_marker -> 00 00 -> kind=3`。因此不能再把
“资源未下载”或“记录插入在 marker 之后”当作根因。

最初以为 `LoadSceneDataFromStream(0x01006204)` 的实体回调是下一处边界；但运行时
入口记录已经证明，它来自 `sub_100DEB4` 为 `empty.sce` 创建的占位 scene-layer，
第八参数 callback 固定为零，**不是**当前 `00蓬莱仙岛_02.sce` 的实体加载链。因此
不得再以该空回调解释或修改小猴子的 kind-3 字节。

当前仍已确认的最早业务偏离是：`SendNPCInteractReq(0x01037ED4)` 未在其 25 行
scene-node table 找到 `monster_id=1000`，故发送 `index=0`。下一取证点是该函数入口
所扫描的实际节点快照，用于确定当前场景使用的是旧静态节点、遗漏新增 kind-3，还是
SCE 更新没有进入正确的节点构造生命周期；在该快照到位前，不再改变挑战响应或离线索引。

为取得这一边界，客户端新增了环境变量开关的只读取证：
`CBE_TRACE_SCE_ENTITY_CALLBACK=1` 时，会同时在
`InitSceneLayerCtrl` 的入口 `0x010067F0`、`LoadSceneDataFromStream` 的入口
`0x01006204` 与其原生 `BLX callback` 指令 `0x010064B2` 前写入
`logs/sce-entity-callback.log`。控制器入口记录上层 caller、SCE resource 及第八参数
callback；loader 入口记录 controller、resource、callback 和 caller；回调点记录 callback
地址、stream offset、SCE map 名及后续字节。因此可区分“当前加载链没有走该 loader”
“上层传了空 callback”和“实体 callback 收到的记录不符合预期”。探针不写 guest memory、
寄存器、PC、LR 或网络响应；该记录将用于 IDA 反编译准确恢复实体回调契约。

同一开关还会在 `SendNPCInteractReq(0x01037ED4)` 入口写出 25 个 scene-node 的
`active/kind/id/x/y` 快照，并以 `*` 标出请求的 actor ID。该快照只读取客户端正常维护的
节点表；它不创建节点、不改 index，也不触碰挑战请求或下行包。

## 7. 实现结果

1. `mock_server_scene_task.c` 的 scene-battle-monster 草稿、原生 SCE2 parser、
   部署编译器与自检均改为要求并写出 effect Actor 的原生 `3,3,len` 尾部信封。
   缺 effect tail 或把长度误作 field ID 的 kind-3 字节都不能通过
   服务端验证。
2. 在 `server_scene_battle_monsters` 上增加了可重复执行的 MySQL 迁移。旧草稿
   使用原生早期场景样本中明确存在的 `e_ghostfireR.actor`，新建和编辑界面也显示
   “效果 Actor（field18）”供运营调整。部署时会和主 Actor 一样验证并发布该依赖。
3. 现有部署的指纹会因 field18 变化变为“存在未部署变更”；需在“场景战斗怪”中
   对对应场景点击“验证并部署”。这会生成新的内容 release，客户端完整重启后经
   WT18/9 → WT18/8 → WT18/7 安装新版 SCE。没有伪造 index、4/10 或客户端状态。
4. “游戏内容更新管理”现分为启动 CBM、场景内容 release、按需具名资源三层；场景
   区显示 release ID、校验和、文件清单、安装路径与“完整重启后安装”的状态。SCE
   被发布时会自动进入这一层，不再被误当作单纯按需资源。

## 8. 验证清单

- [x] `scripts/scene-battle-monster-field18-regression.c`：完整 field14–18
  记录可由同一生产 parser 读回；删除 field18 后 parser 必须拒绝。
- [x] `make -j2`：通过。
- [x] 回归命令：
  `gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY -g -O2 -std=gnu11 -ffunction-sections -fdata-sections -w scripts/scene-battle-monster-field18-regression.c src/gifDecode.c src/mystd.c src/mysql-client.c src/md5.c -o obj/server/scene-battle-monster-field18-regression.exe -Wl,--gc-sections -lpthread -liconv -lm -lkernel32 -lws2_32`，随后运行该 EXE；输出
  `scene battle monster field18 regression passed`。
- [x] 发布后的内容版本会在更新管理以 release ID、校验和和文件清单呈现。
- [ ] 客户端 action13 上行 index 非零，且匹配服务端读回的 live-node index。
- [ ] 响应为既有 1/2/2 + 1/4/5，客户端进入战斗并显示小猴子 Actor。
- [x] 没有强写客户端全局状态、PC、LR 或节点内存。

## 9. 2026-08-11：已安装资源的节点快照与 field18 语义修正

重新进入已安装更新的 `00蓬莱仙岛_02.sce` 后，`SendNPCInteractReq`
入口的只读节点快照为：

```text
active=[0:k0:id10001@376,129;
        1:k3:id4294967295@87,118; 2:k3:id4294967295@374,247;
        3:k3:id4294967295@435,375; 4:k3:id4294967295@67,424;
        5:k1:id20020@308,125; 6:k1:id20021@376,125; 7:k1:id30006@120,120]
```

这证明四个静态摆放节点和三个 `27/11` NPC 节点均已按当前场景资源创建；并不存在
`kind=2/id=1000`。所以“同名 SCE 未安装”不是本次复现的首因，服务端拒绝
`action13(index=0)` 也仍是正确行为。

将所有服务端原生 SCE2 的 491 条可解析 kind-3 记录逐条核对后，field18 的原生退场
资源只出现 `e_ghostfireR.actor`（251 条）、`e_ghostfireG.actor`（223 条）、
`e_ghostfireB.actor`（15 条）和 `e_ghostfiresG.actor`（1 条）。当前小猴子是唯一
使用 `f_blood1.actor` 的记录；该资源是战斗受击效果，并非 SCE 场景怪退场效果。
这是完整 type-2 容器和完整 field14–18 字节序列之后，当前记录相对于原生集合的第一处
未受约束的语义偏离。

修复将 field18 从“任意 Actor”收紧为上述原生集合。已有的 `f_blood1.actor` 草稿进行
一次明确迁移到 `e_ghostfireR.actor`；其他未知值不会被静默改写，而会在部署校验中拒绝。
后台也将其标为“退场特效（field18）”并只显示该原生列表。迁移后的草稿必须再次部署，
使客户端安装新的 SCE；下一个验收点是节点快照中出现 `kind=2/id=1000`，再验证既有
`4/5` 场景战斗分支。

## 10. 下一取证点：公共 scene-node 构造入口

field18 迁移和新版 SCE 安装后，节点快照仍缺少 `kind=2/id=1000`，故不能继续靠调整
挑战提示、请求 index 或下行战斗包尝试修复。新的第一边界是 CBE 的公共
`scene_node_find_or_create(0x0100EFC4)`：若小猴子原始记录达到这个入口，说明 SCE
解析接受了该记录，问题在返回节点之后的类别/资源生命周期；若根本没有 `actor=1000`
的调用，问题在该静态战斗记录的前置字段或所属 parser 分支。

在既有 `CBE_TRACE_SCE_ENTITY_CALLBACK=1` 开关下，客户端现在只读记录该函数对一个
目标 actor 的调用。默认目标为 `1000`，可用
`CBE_TRACE_SCE_NODE_ACTOR_ID=<monster_id>` 覆盖。记录包含寄存器和 ARM ABI 栈参数
（坐标、visual group/variant、三段文本、目标坐标、flags）、调用者 LR 与 SP，并输出到
`logs/sce-entity-callback.log`。探针不修改 guest memory、寄存器、PC、LR、节点表、
网络包或函数返回值。下一次完整重进目标场景并点击小猴子后，应以该记录证明
`actor=1000` 是否进入公共构造路径，再决定是否需要继续反编译正确的静态战斗记录
parser。

## 11. 2026-08-11：静态流终点与退场 Actor 的最终字节校正

本次实际重进后的只读取证没有任何 `scene_node_find_or_create(actor=1000)`
入口记录；结合 action13 的节点快照，可确定 CBE **根本未读取到**新增怪物记录，
而不是创建后再释放。随后直接逐字节复核实际原生 SCE2 文件
`01桃花岛_01.sce`、`01桃花岛_02.sce` 和 `06野猪林_01.sce` 的 kind-3 怪物记录，
确认 field17 后的原生尾部为：

```text
... [03 00][11 00][actor_len][actor] [03 00][03 00][effect_len][effect]
```

两个 `03 00` 都是原生的 little-endian kind marker；其后的首字节才是 effect
Actor 的长度。此前临时参照的导出检查结果把该段错误地拆成 `3,len`，并诱发一次把
第二个 `u16 3` 删除的错误部署。该短尾资源为 `payload=407/raw=424`，客户端收到
WT18/7 后在**安装资源、尚未进入场景**即崩溃，正是本轮最早的可观察偏离。

因此生成器和 production parser 统一恢复并锁定为 `3,3,len`；回归脚本独立检查输出
字节，而不允许同构的错误 emitter/parser 自行“验证”通过。

另一个已确认根因仍是插入位置：`00蓬莱仙岛_02.sce` 的原始末尾是完整的
`kind=8,x,y,1,1,value,0` 场景控制记录。客户端静态实体读取到它便结束；旧部署器把
新记录追加在该 record **之后**，所以 WT18 安装、服务端离线扫描和内容版本都正确，
但客户端不会看见新增字节。这个结论与客户端没有进入公共 node factory 的运行时记录
相互验证。

部署器现只在该精确的最终 kind-8 record 之前插入战斗怪；没有此终止 record 的原生
场景仍以 EOF 为边界。部署验证、行匹配与节点计数均使用相同边界，避免再次把客户端
不会扫描的尾部误算为有效。修复后的有效部署预计为 `payload=409/raw=426`：比原始
场景增加完整 73 字节记录，且记录在 kind-8 终止控制项之前。必须重新部署并由客户端
重新获取该资源，不能继续使用短尾版本。
