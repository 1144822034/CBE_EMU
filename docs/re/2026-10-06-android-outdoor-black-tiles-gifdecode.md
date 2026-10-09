# Android 户外黑地 / 登录框透底：资源契约

日期: 2026-10-06

## 对比 `1b07f8a`

提交版：`GetFile` **包优先**；`JHOnlineData` 解析曾只匹配裸 `.dsh`。

## 现象

1. 户外地砖黑、聊天黄条：曾把磁盘 GIF 抢在包前，或解包时丢掉 type。
2. 登录框白底但背后标题菜单透出：`UI8.gif` 等包未命中后，`fopen("UI8.gif")` 找不到 `JHOnlineData/UI8.gif`。

## 正确契约

1. `GetFileID` / `GetFile`：**包优先**。
2. 包未命中：裸名 `.gif/.map/.sce/.actor/.dsh/.cbm`（及已带 `JHOnlineData/` 前缀的 fopen）按序解析：
   - Android：`../dist-hot/files/JHOnlineData/<name>`（cwd=`filesDir/JHOnline`）
   - 然后：`JHOnlineData/<name>`
3. 磁盘文件：`LE u32 长度 + type + payload`，只剥长度，**保留 type**。
4. `gifDecode`：256KB 读入，缓冲放堆上（Android 线程栈）。

## 续记：2026-10-09

手机 logcat 标题 GIF 已是 `GetFileID miss … cacheId=0x7000+`（磁盘回退命中），Cpu 弹窗发生在其后的 `empty.sce` miss。对照源码时发现文档中的 **dist-hot 优先** 与 **gifDecode 堆缓冲** 未落在 `vmFunc.c`，已补回 `vm_jhonline_resolve_existing` / Android heap `gifDecode`（`src/vmFunc.c` 与 JNI 副本同步）。

## 验证

覆盖安装后：标题登录框应遮住菜单；户外应有地砖。热更层有文件时应以 `../dist-hot/files/JHOnlineData/` 为准。
