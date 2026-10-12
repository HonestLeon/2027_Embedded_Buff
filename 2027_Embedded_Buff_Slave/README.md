# 2027 Embedded Buff —— 从机（打符灯板）固件工程

> 本目录是**从机工程**，由主机工程在 P3 阶段的快照 + 已完成的 CubeMX 全量配置组成。
> **CubeMX 已配置并校验通过**（6 通道 PWM + 6 条 DMA 全部 `Memory To Peripheral`、
> 10 路 EXTI、CAN1 1Mbps + ART/ABOM、NVIC 优先级、USER CODE 区保留）。

## 开工先读这几份（都在 `../docs/`）

| 文档 | 内容 |
|---|---|
| [`../docs/从机代码重构计划.md`](../docs/从机代码重构计划.md) | **路线图**：阶段 S0~S8、里程碑、风险、DoD、可复用资产清单 |
| [`../docs/从机重构清单.md`](../docs/从机重构清单.md) | **逐项明细**：CubeMX 校验结论、逐文件改动、协议时序要求、与老从机差异、台架验收判据 ①~⑭ |
| [`../docs/从机S7台架测试指南.md`](../docs/从机S7台架测试指南.md) | **S7 上板前必读**：五组串行的操作步骤、Watch 判据与记录表 |
| [`../docs/从机CubeMX操作说明.md`](../docs/从机CubeMX操作说明.md) | CubeMX 操作与校验命令（**已完成**，留档备查） |
| [`../docs/从机灯带自检与排障.md`](../docs/从机灯带自检与排障.md) | 可选排障工具：逐条点亮定位灯带问题（**从机未接拨码 → 只能宏触发**）|

配套参考：`代码架构设计.md`（分层与调度）、`能量机关规则.pdf`（5.5.2.1 状态定义）、
`白光测试指南.md`（通道扫描法，从机开工第一步）、`P3调试指南.md`（灯带不亮排查）。

## 快速开始

**进度：S0~S3 ✅ / S4~S6 代码 ✅（2026-10-10，语法 0/0 + 协议段 diff 一致）→ 台架只在 S7 一次做**（进度条 n/5 归 S8 专项）。

1. Keil 打开 `MDK-ARM/*.uvprojx`，Rebuild 一次确认（语法/类型已用
   `arm-none-eabi-gcc -fsyntax-only -Wall -Wextra` 验过：0 error 0 warning；链接与烧录仍需你在 Keil 里跑）；
2. **S2 无需标定**：硬件与 2025 相同 ⇒ 灯数（87/262/320/120）与"索引↔功能"映射**直接继承老从机**，
   `SLV_STRIP_*` 已按老代码 DMA 绑定核对一致；PB8 按老行为置 0（不驱动）。
   只有在**出问题时**（某条不亮/段位不对）才用逐条点亮排查 →
   [`../docs/从机灯带自检与排障.md`](../docs/从机灯带自检与排障.md)（**从机未接拨码，只能用 `DEBUG_TEST_MODE=1` 宏触发**）；
3. 下一步：Keil **Rebuild + 烧录**一次（确认链接与 map 尺寸），然后按《从机代码重构计划》S7 五组串行台架验收
   （组 1 收帧 → 组 2 击打 → 组 3 状态机 → 组 4 外观 → 组 5 联机/烤机），S7 全绿后再开 **S8 进度条专项**。

## 从机代码改动要点（速查）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `config/config_board.h` | ✅ `BOARD_ROLE_MASTER=0`；新增 **`SLAVE_PANEL_NUM`**(1~5，越界 `#error`)，**每块板烧录前改这一处** + 固件文件名带板号；⚠ 本板未接拨码 → SW 触发不可用，`TEST_TRIGGER_USE_SW2=0`，自检只能 `DEBUG_TEST_MODE` 宏 |
| 2 | `config/config_ws2812.h` + `bsp/bsp_ws2812.c` | ✅ 灯带表 1 条 → **6 条通道(5 条接灯带)**，灯数单点定义 `WS_LED_NUM_*` = 87/262/320/120/92/0（**硬件与 2025 相同 → 直接继承老从机口径，无需标定**；PB8 老代码从不驱动故置 0）；缓冲同源推导，实测 `.bss` **46,208B ≈ 45.1KB** / 128KB（scatter 里没有 CCM，不会误放） |
| 3 | `config/config_motor.h`、`application/motor.c/.h` | ✅ **已删**；`main.c` 的 init、`it.c` 的调度、Keil Group（`.uvprojx`/`.uvoptx`）均已去掉 |
| 4 | `application/buff.c` | ✅ **S5 已落地**：协议段（`buff_frame_pack/parse`、`buff_send_frame`、`buff_slave_report_parse`）**逐字节未动**（diff 已核）；从机状态机 `SLV_SLEEP/ARMED/ACTIVATED/ALL_ON`（1ms 消费 `my_ev_*` 帧转换 + 4Hz 击打/上报），`g_dbg_slv` 交付 |
| 5 | `application/hit_detect.c/.h` | ✅ **S4 已落地**：EXTI 只记录 + 4Hz 查表判定 + `HD_ST_*` 显式状态 + `HD_IGNORE_MS` 二道防线，`g_dbg_hit` 交付；**RDET9 环数=1 按老原值写表，待 S7 组 2 实测定案** |
| 6 | `application/light.c` | ✅ **S6 已落地**：灯板图案库（待激活 19 段+箭头 11 份流动 / 环数含 1 环六段特例 / 已激活两条灯臂整条常亮 / 全激活上下两条），**纯复刻老从机**；进度 n/5 归 S8；`g_dbg_pat` 交付；标定值继承 `config_light.h` 未改 |
| 7 | `Core/Src/stm32f4xx_it.c` + `application/buff.c` | ✅ **S3.1**：1ms 节拍里 `buff_can_rx_poll()` 排空 CAN1 → 目标状态写进 `g_buff_rx`；主机状态机段已用 `#if BOARD_ROLE_MASTER` 在从机侧关闭 → **从机只收不发**，总线上不会再有从机发出的 `0x161`。4Hz 只跑 `buff_step()`(S5 消费事件) 与灯效。**禁止 `HAL_Delay` 阻塞** |
| 8 | Keil | ✅ `motor.c` 已删；`hit_detect.c` 已加入 `application` Group（`.uvprojx`/`.uvoptx` 同步）；包含路径不变 |

> 本表只是**入口速查**；逐条依据、改动理由与验收口径以《从机重构清单》§三/§五/§六为唯一出处，
> 阶段划分与工期以《从机代码重构计划》为准。三处冲突时以后两者为准（本表随它们更新）。

## ⚠️ 两条硬约束（不满足会静默失效）

1. **CAN 必须 1ms 级排空**：大符轮次切换时主机先发 `0x01`(熄灭旧对)、**150ms 后**发 `0x02`(点亮新对)；
   若从机 4Hz 才处理，熄灭帧会被后一帧覆盖 → 旧灯板不熄（老从机 200ms 单缓冲的固有缺陷，
   实测表现为"熄灭不稳定"）。
2. **禁止阻塞**：老从机主循环 `HAL_Delay(200)`，新从机用 TIM6 1ms 调度 + 4Hz 逻辑帧。

## 主从共享文件（改动必须两边同步）

| 文件 | 共享内容 |
|---|---|
| `application/buff.c/.h` | 协议段：帧结构、`buff_frame_pack/parse`、`buff_send_frame`、`buff_slave_report_parse`（**逐字节保持与主机一致**）。当前从机版另有两处差异：① 新增 S3.1 接收路径 `buff_can_rx_poll()` / `g_buff_rx`；② 主机状态机整段被 `#if BOARD_ROLE_MASTER` 包住（从机侧不编译，S5 换成从机状态机）|
| `bsp/bsp_can.c/.h`、`bsp/bsp_ws2812.c/.h`、`bsp/bsp_switch.c/.h`、`bsp/bsp_rng.c/.h` | 板级驱动 |
| `config/config_can.h`、`config/config_ws2812.h`（时序部分）、`config/config_light.h`（标定值） | 参数与标定 |

> 同步方式：与主机对应文件 `diff` 确认；**标定数值只允许搬位置、不允许改值**。
