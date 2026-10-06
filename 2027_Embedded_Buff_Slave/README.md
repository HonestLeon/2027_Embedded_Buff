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

配套参考：`代码架构设计.md`（分层与调度）、`能量机关规则.pdf`（5.5.2.1 状态定义）、
`白光测试指南.md`（通道扫描法，从机开工第一步）、`P3调试指南.md`（灯带不亮排查）。

## 快速开始

1. Keil 打开 `MDK-ARM/*.uvprojx`，Rebuild（此刻仍是主机快照代码，能编过）；
2. **第一步做通道扫描**：把 `config/config_board.h` 的 `DEBUG_TEST_MODE` 置 1 编译烧录，
   Watch `g_dbg_ws_scan_idx`，逐条记录"索引 ↔ 物理灯带 ↔ 灯数"（详见《从机重构清单》S2）；
3. 扫描完成后回填 `config/config_ws2812.h` 灯带表与灯数，再进入 S3~S6 的开发。

## 从机代码改动要点（速查）

| # | 文件 | 改动 |
|---|---|---|
| 1 | `config/config_board.h` | `BOARD_ROLE_MASTER` → **0**；新增 **`SLAVE_PANEL_NUM`**(1~5)，**每块板烧录前改这一处**（沿用老方式按板编译，固件文件名带板号） |
| 2 | `config/config_ws2812.h` + `bsp/bsp_ws2812.c` | 灯带表 1 条 → **5~6 条** + 补回缓冲（881 灯 ≈43KB，**必须主 SRAM，不能用 CCM**） |
| 3 | `config/config_motor.h`、`application/motor.c` | **删除**（从机无电机）；同时从 `main.c` 的 init、`it.c` 的调度、Keil Group 里去掉 |
| 4 | `application/buff.c` | 保留协议段（`buff_frame_pack/parse`、`buff_send_frame`、`buff_slave_report_parse`）**逐字节不动**；把"主机构建专用"的状态机段换成从机状态机（`SLV_SLEEP/ARMED/ACTIVATED/ALL_ON`） |
| 5 | `application/hit_detect.c/.h` | **新增**：EXTI 采集 + 环数查表 + `HD_ARMED/FIRED/LOCKED/MISHIT` 显式状态；**RDET9 环数必须实测核对** |
| 6 | `application/light.c` | 换成灯板图案库（待激活+箭头流动 / 环数 / **中部灯臂进度 n/5（规则要求，老从机未实现）** / 全激活）；标定值继承 `config_light.h` |
| 7 | `Core/Src/stm32f4xx_it.c` | 1ms 节拍里 **排空 CAN**（`bsp_can1_pop`）+ 灯带刷新；4Hz 帧跑状态机与图案。**禁止 `HAL_Delay` 阻塞** |
| 8 | Keil | `application` Group 加 `hit_detect.c`、删 `motor.c`；包含路径不变 |

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
