# 2027 Embedded Buff（能量机关固件）

2026/2027 赛季 RoboMaster 能量机关（大符/小符）固件。
STM32F405RGT6 + HAL，Keil MDK-ARM 工程，由 STM32CubeMX 6.15 生成。

主从共用同一块 PCB（原理图见上级目录 `原理图/`），**分两个 CubeMX 工程分别构建**。

## 工程结构

```
2027_Embedded_Buff/
├── 2027_Embedded_Buff_Master/          # ★ 主机（中心板）：电机 + 中心灯带 + 灯板调度
│   ├── 2027_Embedded_Buff_Master.ioc   # CubeMX 配置（引脚/时钟/外设）
│   ├── Core/                           # 生成代码 + USER CODE 区
│   ├── Drivers/                        # HAL / CMSIS / DSP 库
│   ├── config/                         # 配置层：纯宏与 const 表（6 个头文件）
│   ├── bsp/                            # 板级支持包：ws2812 / can / switch / rng
│   ├── application/                    # 业务层：motor / buff / light
│   └── MDK-ARM/                        # Keil 工程（.uvprojx）
│
└── 2027_Embedded_Buff_Slave/           # ☆ 从机（打符灯板）：起手模板（尚未开发）
    ├── README_从机改动清单.md          #   ← 从机开工先读这个
    ├── 2027_Embedded_Buff_Master.ioc   #   基于主机配置的快照，需按清单改
    ├── Core/ config/ bsp/ application/ #   与主机同构
    └── MDK-ARM/                        #   Drivers/ 未包含，CubeMX 生成时自动补齐
```

> 分层规则：`config`（只放数）→ `bsp`（硬件操作 + 中断收尾）→ `application`（业务逻辑）。
> 依赖只准向下；`Core/` 只动 `USER CODE` 区。详见上级目录《代码架构设计.md》。

## 快速开始（主机）

1. Keil 打开 `2027_Embedded_Buff_Master/MDK-ARM/2027_Embedded_Buff_Master.uvprojx`，Rebuild；
2. 需要改外设配置时用 STM32CubeMX 打开工程根目录的 `.ioc`（KeepUserCode 已启用）；
3. 烧录方式：SWD（PA13/PA14）。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 工程接入（config/bsp/application 分层骨架、TIM6 调度入口） | ✅ |
| P1 | config 六件套（时序/参数/标定值） | ✅ |
| P2 | `bsp_ws2812` 完整驱动（PWM+DMA / dirty-busy 握手 / 帧完成回调） | ✅ |
| P3 | 灯带点亮与通道定位（实测：主机仅 1 条灯带 = 中心 PA6/88 颗） | ✅ |
| P4 | `bsp_can`（双总线过滤器+双队列+零等待发送）+ buff 协议 pack/parse | ⬜ |
| P5 | `motor`（双环 PID + 大符正弦前馈） | ⬜ |
| P6 | `buff` 大小符状态机 + 随机序列 | ⬜ |
| P7 | 整机联调与验收 | ⬜ |

## 硬件要点速查（主机）

- MCU：STM32F405RGT6，HSE 8MHz → SYSCLK 168MHz（**APB1 Timer 84MHz**）
- **灯带（实测）：只有 1 条 —— "中心"，TIM3_CH1 / PA6 / 88 颗**（板上丝印标的是 DIN6）；
  PWM+DMA 驱动，bit 率 800kHz（PSC=2, ARR=34），T0H=10 / T1H=25，复位段 90bit。
  其余 5 个通道（PA7 / PB1 / PB6 / PB7 / PB8）实测**未接灯带**，CubeMX 中已裁掉
- CAN1（PA11/PA12）：主从互联，`0x161` 发指令 / `0x160` 收回报，1Mbps
- CAN2（PB12/PB13）：电机，`0x200` 发电流帧 / `0x201~204` 收反馈（波特率待实测确认）
- 拨码 ×3：`PD2` 大小符 / `PB3` 停转 / `PB4` 红蓝（**实测 OFF/下=低、ON/上=高**；高=转动/蓝/大符）
- 板卡身份 ID1~3：`PC11 / PC12 / PB9`（外部 10k 上拉 + 三挡拨动开关拉低）
- 1ms 调度：TIM6（PSC=83, ARR=999），中断内跑电机环 + 灯带刷新 + 250 分频的 4Hz 逻辑帧

## 调试小工具

| 变量 | 位置 | 用途 |
|---|---|---|
| `g_sched_tick_cnt` | `Core/Src/stm32f4xx_it.c` | 调度心跳：1ms 中断每 tick +1，为 0 或不动 = 中断没进 |
| `g_dbg_ws_scan_idx` | `application/light.c` | 灯带通道扫描：SW2=ON 进扫描时，该值 = 当前点亮灯带的索引（0xFF=未扫描） |

## 文档（均在上级目录 `2026_Embedded-Buff_new/`）

| 文档 | 内容 |
|---|---|
| 《代码架构设计.md》 | 分层架构、模块职责、依赖规则、调度设计（**开发前先读**） |
| 《重构对照-旧代码与新设计.md》 | 2025 老固件逐行分析、标定数据继承核对表 |
| 《主机CubeMX配置清单.md》 | CubeMX 每一项配置的依据、待确认项、自检清单 |
| 《CubeMX裁剪说明.md》 | 主机为何/如何裁掉冗余外设；从机要加回的完整清单 |
| 《白光测试指南.md》 | 灯带通道扫描定位法（换板/改线/从机开工必做） |
| 《P3调试指南.md》 | 灯带不亮的 debugger 排查流程（含 DMA 方向踩坑记录） |
| 《代码重构计划.md》 | 分阶段实施计划、里程碑、验收标准 |
| 《大符思路.txt》 | 2025 赛季需求与修改记录（原维护者手记） |
