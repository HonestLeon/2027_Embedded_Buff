# CubeMX 校验与踩坑（主从通用）

> 用途：改过 `.ioc` 或重新生成代码之后，确认没有被 CubeMX 改坏；以及三个反复踩过的坑。
> 本文只讲**怎么验证**和**坑怎么判**；每一项配置"该配成什么值、依据是什么"的唯一出处是
> 《主机CubeMX配置清单.md》（从机差异见其 0.1 节与本文表格的"从机"列）。
> 合并自原《CubeMX裁剪说明》《P3调试指南》《从机CubeMX操作说明》三份（2026-10 文档整理）。

---

## 一、生成后必查清单

在工程根目录（`2027_Embedded_Buff_Master/` 或 `2027_Embedded_Buff_Slave/`）执行，两列期望值分别对应主机、从机。

| 检查 | 命令 | 主机期望 | 从机期望 |
|---|---|---|---|
| 启用的 IP 清单 | `grep -E "^Mcu\.IP[0-9]" *.ioc` | 9 项，**不含 TIM4** | 10 项，含 TIM4 |
| DMA 请求条数 | `grep -E "^Dma\.Request" *.ioc` | 1 条（TIM3_CH1/TRIG） | 6 条（TIM3_CH1/CH2/CH4 + TIM4_CH1/CH2/CH3） |
| DMA 方向 | `grep -E "^Dma\..*Direction" *.ioc` | 全部 `DMA_MEMORY_TO_PERIPH` | 同左 |
| 生成代码里的方向 | `grep -c "Init.Direction = DMA_MEMORY_TO_PERIPH" Core/Src/tim.c` | 1 | 6 |
| 引脚没有被偷偷加回来 | `grep -A 6 "TIM3 GPIO Configuration" Core/Src/tim.c` | 只出现 PA6 | PA6、PA7 |
| 中断向量数量 | `grep -n "^void .*IRQHandler" Core/Src/stm32f4xx_it.c` | 5 个（CAN1_RX0、CAN2_RX0、TIM6_DAC、DMA1_Stream4、SysTick） | 6 个 DMA + 6 个 EXTI + CAN×2 + TIM6 |
| 击打输入引脚 | `grep -c "define KEY[0-9]*_Pin" Core/Inc/main.h` | — | 10 |
| 节拍回调还在 | `grep -n "USER CODE BEGIN 1" -A 8 Core/Src/stm32f4xx_it.c` | 有 `HAL_TIM_PeriodElapsedCallback` 与 `g_sched_tick_cnt` | 同左 |
| 编译 | Keil Rebuild | 0 Error 0 Warning | 同左 |

两条注意：

- 重新生成会重写 `Core/` 的生成代码，`USER CODE` 区内的内容不动；但 `stm32f4xx_it.c` 顶部的
  `#include "config_board.h"` 等一旦丢失要补回，否则调度器编译不过。
- 观察变量要用 `-O0` 或 `-O1`（Keil 的 Level 0/1），`-O2` 以上会把变量优化掉导致 Watch 看不到。

## 二、关键数值速查

| 项 | 值 | 备注 |
|---|---|---|
| 时钟 | HSE 8MHz，/M=4、×N=168、/P=2 → 168MHz；APB1=42MHz、APB2=84MHz、**APB1 Timer=84MHz** | 坑：RCC 里开 Crystal **且**时钟树把 SYSCLK 源切到 HSE，两步缺一仍跑内部 HSI |
| 灯珠时基 | TIM3/TIM4 六路一致 `PSC=2`、`ARR=34` → 28MHz/35 = 800kHz；`T0H=10`(357ns)、`T1H=25`(893ns)；复位段 90 bit ≈112µs | 备选 `PSC=0/ARR=104` 未实测，不采用 |
| 系统节拍 | TIM6 `PSC=83`、`ARR=999` → 1kHz | 20ms 采开关、250 分频得 4Hz 逻辑帧 |
| CAN 位率 | `Prescaler=3`、`BS1=10TQ`、`BS2=3TQ` → 42MHz/3/14 = 1Mbps，采样点 78.6% | `AutoRetransmission`、`AutoBusOff` 都 Enabled |
| CAN1 引脚 | 必须手确认 **PA11=RX、PA12=TX** | CubeMX 默认可能给 PB8/PB9，而 PB8=TIM4_CH3、PB9=板卡 ID3 |
| NVIC 分组 | 4 bits for pre-emption | 数值以 `.ioc` 为准：CAN1_RX0 = 1/0、CAN2_RX0 = **1/0**、TIM6_DAC = 2/0、EXTI 六路 = 3/0、DMA1 六个流 = 5/0、SysTick = 15/0；优先级顺序 CAN > 节拍 > EXTI > DMA > SysTick。旧文档把 CAN2 写成 1/1 是笔误 |
| 击打输入 EXTI | 十路全部 **Falling + No pull**（外部 1kΩ + 10kΩ + 10µF） | PA5 与 PC6~PC9 共用 `EXTI9_5`，回调里按 `GPIO_Pin` 分发 |
| 拨码极性 | 实测 OFF/下 = 低、ON/上 = 高 → 三个 `*_ACTIVE_LEVEL = 1` | 曾按旧注释假设低有效，导致测试模式与红蓝同时反向 |
| 灯珠缓冲位置 | 必须在主 SRAM，**DMA 不能访问 CCM(64KB)** | 主机 (88×24+90)×2B ≈ 4.4KB；从机 881 颗 ≈ 43KB，余量 ≥40KB |
| 调试口 | SYS Debug 必须 `Serial Wire` | 否则二次烧录会锁住，且 PB3/PB4 需被释放给开关 |
| 上电时序 | 上电 `HAL_Delay(100)` 后再刷灯 | — |
| 从机 CAN2 | 保留 | `bsp_can.c` 主从共用，删掉编译不过（见《遗留待办》H6） |

## 三、坑一：DMA 方向配反（代价最大的一次）

- 正确：`Memory To Peripheral`（生成代码里是 `DMA_MEMORY_TO_PERIPH`），外设地址增量不勾、
  内存地址增量勾、两侧 Half Word、Mode Normal、FIFO Disable、优先级 Low。
- 配反的机理：`HAL_DMA_Start_IT` 会把 `PAR` 指向 `ccr_buf`、`M0AR` 指向 `&TIM3->CCR1`，
  于是 PINC=0 让 DMA 永远只读 `buffer[0]`，而 MINC=1 让它每写一次地址 +2，依次扫过
  CCR1 → CCR2 → CCR3 → CCR4 → BDTR → DCR → DMAR → OR，再冲进 TIM4、TIM5、**TIM6**、TIM7
  的寄存器区。表现是灯不亮、花屏、甚至莫名死机。
- 铁证：`TIM3->DMAR` 读到 `0x00000001`，而代码里从没写过这个寄存器。
- 判定方法：**以能正常工作的老工程的生成代码为准**。`.ioc` 里 Direction 的历史值可能与
  实际生成的代码不一致，只核对 `.ioc` 会漏判。
- 修好之后必须**断电重启**：寄存器已经被砸坏，不复位看不出区别。

## 四、坑二：EXTI 用双边沿

一次击打产生 2 个沿 → 第二个沿被当成"打错" → 从机上报 `[2]=0x01` → 主机整轮复位。
收敛为 Falling 之后仍保留软件二道防线：`HD_IGNORE_MS` 取 50~100ms，且不在名单内的引脚只计数不判定。

## 五、坑三：CAN 的默认值

- 引脚默认可能是 PB8/PB9，与 TIM4_CH3、板卡 ID3 冲突，必须手改 PA11/PA12。
- 旧 `.ioc` 里 CAN2 写的是 100kbps，属陈旧值（曾误配成 P=28/BS1=11/BS2=3），按代码依据改回 1Mbps，
  实测复核仍挂在《遗留待办》H4。
- 过滤器用 MASK 模式：CAN1 只收 `0x160~0x161`、CAN2 收 `0x201~0x204`，全部指向 FIFO0，
  因此只需开 RX0 中断；RX 中断里只做取报文和入队。

## 六、灯带不亮时的调试判定表

先分岔：Watch `g_dbg_isr_cnt`（或 `g_sched_tick_cnt`）是否每 1ms +1。不涨说明没进中断，
去查 TIM6 与 NVIC；在涨再按下表查灯带链路。

| 观察对象 | 正常期望 | 不符时说明 |
|---|---|---|
| `TIM3->CCR1` | 在 10 与 25 之间变化 | 没变化 = PWM 没启动或数组没填 |
| `TIM3->CNT` | 0~34 循环 | 超出 = PSC/ARR 配错 |
| `DMA1_Stream4->NDTR` | 从 2202 递减到 0 | 不动 = DMA 没启动；不递减到 0 = 方向或长度错 |
| `DMA1_Stream4->PAR` | `&TIM3->CCR1`（0x40000434） | 指向 buffer = 方向配反（见第三节） |
| `GPIOA->MODER` 的 PA6 | `10`（复用功能） | 不是 = 引脚没配成 AF |
| `GPIOA->AFR[0]` 的 PA6 | `2`（AF2 = TIM3） | 不是 = 复用给了别的外设 |
| `TIM6->CR1` 的 CEN/ARPE、`TIM6->DIER` 的 UIE | 均置位 | 缺 = 节拍没跑 |
| `NVIC->ISER[1]` bit22（IRQ 54）、`NVIC->IP[54]` | 置位、`0x20` | 缺 = 中断没使能或优先级错 |
| 死机时看 `SCB->HFSR`、`SCB->CFSR` | `HFSR=0x40000000` 表示 FORCED，再看 CFSR 与调用栈 | 常见成因就是第三节那个 DMA 越界写 |

## 七、相关文档

| 文档 | 用途 |
|---|---|
| `主机CubeMX配置清单.md` | 每一项配置的值与依据（唯一出处），含 0.2 丝印错位结论、附录 A/B/C |
| `灯带白光自检与排障.md` | 逐条点亮定位哪条灯带有问题 |
| `代码架构设计.md` | `bsp_ws2812` 的 dirty/busy 机制、节拍设计 |
| `重构对照-旧代码与新设计.md` | 老代码逐行分析与继承的标定值 |
