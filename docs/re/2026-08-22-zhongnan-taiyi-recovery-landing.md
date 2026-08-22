# 钟南山—太乙峰死亡复活与脱离卡位

> 后续策略更新：普通死亡复活已在同日改为“最近城镇中心”，不再回到最近传送石场景。
> 本文的 MAP 防卡位结论仍适用于脱离和复活落点；现行复活策略见
> 2026-08-22-nearest-town-center-respawn.md。

## 范围与触发

玩家在“钟南山—太乙峰”中选择脱离，或死亡后走普通复活路径时，可能被落在场景右下
传送入口附近的柱子/碰撞格中，角色无法移动。显示名称对应的真实场景资源为
11终南山_02.sce。

本次只调整模拟服务端在**死亡复活**和**设置页脱离**时给出的落点；不改 CBE/CBM
指令、客户端内存、寄存器或普通传送的坐标契约。

## 取证和首个偏离

- 从 web/fs/JHOnlineData/11终南山_02.sce 解出的地图尺寸为 512 x 400，格子为
  16 x 16。右下边缘入口记录的落点为 (427,340)，入口触发矩形为
  (432,368)-(480,384)。
- 旧的 vm_net_mock_get_scene_reasonable_spawn_from_sce() 以地图中心为参照，正好选择
  此入口。旧的 vm_net_mock_adjust_safe_player_pos_for_scene() 为防止再次触发入口，
  将该点移为 (400,340)。
- 对同一资源引用的 MAP 解压后，(400,340) 所在格 (25,21) 的碰撞高半字节为
  0xF（四个边界均阻挡）。其左上邻域也有阻挡格，正是人物体积无法脱困的首个错误
  状态，不是客户端移动处理本身的问题。
- 客户端已有的碰撞实现与此格式一致：CheckMoveCollision(0x01045258) 会组合人物
  占用格；CheckMapMoveCollision(0x010451C2) 和
  CheckMapMoveCollisionY2(0x0104512E) 分别读取 MAP 高半字节的横向/纵向边界。
  相关反汇编证据见 docs/re/2026-06-25-teleport-stone.md。
- 死亡路径虽然先在 sMap.dsh 上按最短场景路由寻找带 n_telestone 的场景，但随后
  仍调用上述通用边缘入口落点选择器；脱离路径也有相同的通用落点选择。因此两条路径
  共享这个错误。

## 修复

mock_server_scene_task.c 新增只供恢复路径调用的 MAP 落点校验：

1. 从真实 SCE2 头取得该场景实际引用的 MAP，并保留实际的边缘入口触发框；
2. 解码 MAP 后，以客户端使用的碰撞高半字节检查候选格；
3. 只接受自身及周围一圈格都没有碰撞边界、且不在任何入口触发框安全间距内的点；
4. 从旧落点向全图选取距离最近的合格格。若 SCE/MAP 无法读取或解析失败，保持原有
   坐标，不伪造默认场景或坐标。

对于太乙峰，旧链路的 (427,340) -> (400,340) 现在会稳定得到 (408,280)。普通
传送仍使用资源中定义的入口坐标，未套用这项恢复修正。

该校验已接入：

- vm_net_mock_resolve_nearest_teleport_stone_respawn() 的普通死亡复活落点；
- 普通死亡复活中原场景安全落点的两个后备分支；
- vm_net_mock_get_current_scene_unstuck_target() 的设置页脱离目标。

## 验证

已执行：

~~~
make -j2
~~~

服务端完整编译、链接通过。

新增 scripts/zhongnan-taiyi-recovery-landing-regression.c，它不启动监听器、不连接
MySQL，也不写入角色数据；只读取生产资源并用内存角色夹具验证：

~~~
gcc -DNETWORK_SUPPORT -DCBE_SERVER_ONLY ... \
  scripts/zhongnan-taiyi-recovery-landing-regression.c ... \
  -o tmp/zhongnan-taiyi-recovery-landing-regression.exe
.\tmp\zhongnan-taiyi-recovery-landing-regression.exe
~~~

测试成功（退出码 0），覆盖以下断言：

- 原入口经防重触发处理后确实复现为 (400,340)，并被 MAP 判为不可用；
- MAP 恢复选择器给出 (408,280)，且其 3 x 3 占用邻域均无碰撞边界；
- 实际普通死亡复活解析器和实际设置页脱离目标选择器都返回太乙峰的
  (408,280)。

仍建议以真实测试账号各执行一次死亡复活和设置页脱离，验收客户端的移动手感；这属于
体验验收，不改变上述资源与协议层的通过证据。
