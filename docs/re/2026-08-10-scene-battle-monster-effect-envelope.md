# 场景战斗怪退场 Actor 子记录

Date: 2026-08-10

Status: corrected 2026-08-11; requires one explicit re-deploy and client re-entry

## 触发

`00蓬莱仙岛_02.sce` 已通过内容更新下载，服务器也能从该文件扫描到小猴子
`monster_id=1000`，但 NPC action13 的 `WT 1/4/1` 仍发送 `index=0`，客户端提示
“挑战目标尚未加载”。这证明问题不再是更新清单、下载或挑战响应：客户端没有把发布的
记录建立为 type-2 场景节点。

## 首个偏离

对比已解压的原生 `01桃花岛_01.sce` 和本次已部署的
`00蓬莱仙岛_02.sce`，field 14–17 相同；field17 后的无编号退场 Actor 子记录不同：

```text
原生： ... [03 00][03 00][len][effect.actor]
错误短尾：... [03 00][len][effect.actor]
```

两段 `03 00` 都是原生记录的一部分；第二段后面的一个字节才是 effect Actor
字符串长度。该尾部不是普通的 `field=18` 字符串字段。短尾资源会使
`LoadSceneDataFromStream` 在安装 SCE 时越过正确记录边界，随后在场景数据读取阶段
访问空对象。服务器 parser 曾被错误地改成与短尾生成器一致，因而把这份无效资源
标记为可部署。

## 修复

`vm_net_mock_scene_battle_monster_append_record()` 现按原生字节写入
`[3][3][len][effect]`。同一生产 parser 只接受该结构，部署校验、状态检测、战斗
出生点选择和怪物目录均使用它。旧 `3,18` 资源不会再被误判为有效；需要在后台对该场景
再次执行“验证并部署”，使发布的 SCE 字节真正变化。

`scripts/scene-battle-monster-field18-regression.c` 同时覆盖：

1. 生成记录可以被生产 parser 读回；
2. 输出包含原生 `3,3,len` 退场 Actor 子记录；
3. 删除完整退场 Actor 或删除第二个 `u16 3` 的短尾部署格式均被拒绝。

## 验证边界

构建与脚本通过只证明服务端发布的是原生记录格式。人工复测仍须确认：重新部署后客户端
下载新的同名 SCE，重新进入 `00蓬莱仙岛_02.sce`，选择小猴子挑战产生 `WT 1/4/1` 的
nonzero `index`；随后既有 `2/2 + 4/5` 分支进入战斗且左侧为 `e_monkey.actor`。不以
服务器离线推导 index 代替该客户端 live-node 证据。
