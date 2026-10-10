# 2027 Embedded Buff —— 从机（打符灯板）固件工程

> 本目录是**从机工程**，由主机工程在 P3 阶段的快照 + 已完成的 CubeMX 全量配置组成。
> **CubeMX 已配置并校验通过**（6 通道 PWM + 6 条 DMA 全部 `Memory To Peripheral`、
> 10 路 EXTI、CAN1 1Mbps + ART/ABOM、NVIC 优先级、USER CODE 区保留）。

## 开工先读这三份（都在 `../docs/`）

| 文档 | 内容 |
|---|---|
| [`../docs/从机代码重构计划.md`](../docs/从机代码重构计划.md) | **路线图**：阶段 S0~S7、里程碑、风险、DoD、可复用资产清单 |
| [`../docs/从机重构清单.md`](../docs/从机重构清单.md) | **逐项明细**：CubeMX 校验结论、逐文件改动、协议时序要求、与老从机 7 处差异、台架验收 |
| [`../docs/从机CubeMX操作说明.md`](../docs/从机CubeMX操作说明.md) | CubeMX 操作与校验命令（**已完成**，留档备查） |
| [`../docs/从机通道扫描指南.md`](../docs/从机通道扫描指南.md) | **S2 通道扫描**：操作步骤、记录表、结果回填、异常排查、验收标准 |

配套参考：`代码架构设计.md`（分层与调度）、`能量机关规则.pdf`（5.5.2.1 状态定义）、
`白光测试指南.md`（通道扫描法，从机开工第一步）、`P3调试指南.md`（灯带不亮排查）。

## 快速开始

**进度：S0 基线 ✅ / S1 config 从机化 ✅（2026-10-06）→ 下一步就是 S2 通道扫描。**

1. Keil 打开 `MDK-ARM/*.uvprojx`，Rebuild 一次确认（语法/类型已用
   `arm-none-eabi-gcc -fsyntax-only -Wall -Wextra` 验过：0 error 0 warning；链接与烧录仍需你在 Keil 里跑）；
2. **S2 通道扫描**：`config/config_board.h` 已把 `TEST_TRIGGER_USE_SW2` 置 1 → **拨 SW2=ON 即进扫描**
   （或把 `DEBUG_TEST_MODE` 置 1 编一个上电即扫描的调试固件）。Watch `g_dbg_ws_scan_idx`，
   逐条记录"索引 ↔ 物理灯带 ↔ 灯数" —— **完整步骤/记录表/回填/排查见
   [`../docs/从机通道扫描指南.md`](../docs/从机通道扫描指南.md)**；
3. 扫描结果**只回填一处**：`config/config_ws2812.h` 的 `WS_LED_NUM_*`（灯带表与 DMA 缓冲都由它推导），
   再确认 S4~S6 要画在哪条灯带上（`config_light.h` 的 `SLV_STRIP_*`）。

## 从机代码改动要点（速查）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `config/config_board.h` | ✅ `BOARD_ROLE_MASTER=0`；新增 **`SLAVE_PANEL_NUM`**(1~5，越界 `#error`)，**每块板烧录前改这一处** + 固件文件名带板号；SW2 挪作白光扫描触发 |
| 2 | `config/config_ws2812.h` + `bsp/bsp_ws2812.c` | ✅ 灯带表 1 条 → **6 条**，灯数单点定义 `WS_LED_NUM_*`（老口径 87/262/320/120/92/0，待 S2 实测）；缓冲随之开出，实测 `.bss` **49.6KB**/128KB（scatter 里没有 CCM，不用担心误放）；**灯数仍是要 S2 标定的标定值** |
| 3 | `config/config_motor.h`、`application/motor.c/.h` | ✅ **已删**；`main.c` 的 init、`it.c` 的调度、Keil Group（`.uvprojx`/`.uvoptx`）均已去掉 |
| 4 | `application/buff.c` | ⬜ 保留协议段（`buff_frame_pack/parse`、`buff_send_frame`、`buff_slave_report_parse`）**逐字节不动**；把"主机构建专用"的状态机段换成从机状态机（`SLV_SLEEP/ARMED/ACTIVATED/ALL_ON`） |
| 5 | `application/hit_detect.c/.h` | ⬜ **新增**：EXTI 采集 + 环数查表 + `HD_ARMED/FIRED/LOCKED/MISHIT` 显式状态；**RDET9 环数必须实测核对** |
| 6 | `application/light.c` | ⬜ 换成灯板图案库（待激活+箭头流动 / 环数 / **中部灯臂进度 n/5（规则要求，老从机未实现）** / 全激活）；标定值继承 `config_light.h` |
| 7 | `Core/Src/stm32f4xx_it.c` | ⬜ 1ms 节拍里 **排空 CAN**（`bsp_can1_pop`）+ 灯带刷新；4Hz 帧跑状态机与图案。**禁止 `HAL_Delay` 阻塞**（当前仍是主机快照的调度内容，S3/S5 一并改） |
| 8 | Keil | ⬜ 删 `motor.c` 已完成；S4 时 `application` Group 加 `hit_detect.c`；包含路径不变 |

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
| `application/buff.c/.h` | 协议段：帧结构、`buff_frame_pack/parse`、`buff_send_frame`、`buff_slave_report_parse` |
| `bsp/bsp_can.c/.h`、`bsp/bsp_ws2812.c/.h`、`bsp/bsp_switch.c/.h`、`bsp/bsp_rng.c/.h` | 板级驱动 |
| `config/config_can.h`、`config/config_ws2812.h`（时序部分）、`config/config_light.h`（标定值） | 参数与标定 |

> 同步方式：与主机对应文件 `diff` 确认；**标定数值只允许搬位置、不允许改值**。
