# 2027 Embedded Buff（能量机关主机固件）

2026/2027 赛季 RoboMaster 能量机关（大符/小符）主机（中心板）固件。
STM32F405RGT6 + HAL，Keil MDK-ARM 工程，由 STM32CubeMX 6.15 生成。

## 工程结构

```
2027_Embedded_Buff/
└── 2027_Embedded_Buff_Master/     # CubeMX 工程
    ├── 2027_Embedded_Buff_Master.ioc   # CubeMX 配置（引脚/时钟/外设）
    ├── Core/                           # 生成代码 + USER CODE 区
    ├── Drivers/                        # HAL / CMSIS / DSP 库
    └── MDK-ARM/                        # Keil 工程（.uvprojx）
```

## 快速开始

1. Keil 打开 `MDK-ARM/2027_Embedded_Buff_Master.uvprojx`，Rebuild；
2. 需要改外设配置时用 STM32CubeMX 打开根目录 `.ioc`（KeepUserCode 已启用）；
3. 烧录方式：SWD（PA13/PA14）。

## 文档

- 《主机CubeMX配置清单.md》—— CubeMX 配置依据与自检清单（仓库外层目录）
- 《重构对照-旧代码与新设计.md》—— 2025 赛季老固件逐行分析与新设计（同上）
- 《原理图/》—— 主从共用板原理图（PDF / SVG）
- 《大符思路.txt》—— 2025 赛季需求与修改记录

## 硬件要点速查

- MCU：STM32F405RGT6，HSE 8MHz → SYSCLK 168MHz（APB1 Timer 84MHz）
- WS2812 ×6 路：TIM3_CH1(PA6)/CH2(PA7)/CH4(PB1) + TIM4_CH1(PB6)/CH2(PB7)/CH3(PB8)，
  PWM+DMA 驱动，bit 率 800kHz（PSC=2, ARR=34），T0H=10/T1H=25
- CAN1（PA11/PA12，主从互联 0x160/0x161，1Mbps）；CAN2（PB12/PB13，电机 0x200）
- 击打检测 KEY1~10：PA1~PA5 / PC6~PC10（EXTI，硬件 RC 消抖）
- 拨码 ×4：PD2(大小符) PB3(停/转) PB4(红/蓝) PB5(调试)；板卡身份 ID1~3: PC11/PC12/PB9
