# 从机 CubeMX 逐步操作说明

> 工程：`2027_Embedded_Buff_Slave/`（主机 P3 快照，已裁掉 TIM4/EXTI 等）
> 目标：把从机需要的 **10 路击打输入 + TIM4 全通道 + TIM3 的 CH2/CH4 + 5 条 DMA 流** 加回来。
> 依据：`docs/从机重构清单.md` 第二节；坑点见 `docs/CubeMX裁剪说明.md` 第五节。
>
> ⚠️ 操作前先备份/提交 git（CubeMX 重新生成会重写 `Core/` 下生成代码，USER CODE 区不动）。

---

## 步骤 0：工程名 —— ✅ 已定：不改名

**结论（2026-10）**：`2027_Embedded_Buff_Slave/` 内部的 `.ioc` 与 Keil 工程名**保持 `2027_Embedded_Buff_Master` 不变**。

- 改名会触发 CubeMX 重生成 `MDK-ARM/`，需要重配 `bsp`/`application` 两个 Group 与
  `..\config;..\bsp;..\application` 三个包含路径 —— 收益只是名字好看，代价是再来一次手工活；
- 区分主从靠**目录名**（`_Slave`）和固件里的 `BOARD_ROLE_MASTER`，不靠 Keil 工程名；
- 由此产生的"从机目录里有个叫 Master 的 .ioc"是**预期状态**，见《遗留待办》R3。

---

## 步骤 1：击打检测输入（10 引脚 + 6 个 EXTI 中断）

**① 配置引脚**：在引脚图上左键点击 PA1~PA5、PC6~PC10，逐个选 **`GPIO_EXTIx`**
（PA1→EXTI1 … PA5→EXTI5；PC6→EXTI6 … PC10→EXTI10）。

**② System Core → GPIO**，按下表逐行设置（10 行）：

| 引脚 | GPIO mode | GPIO Pull-up/Pull-down | User Label | 触发沿 |
|---|---|---|---|---|
| PA1~PA5 | External Interrupt Mode with **Falling** edge trigger | **No pull-up and no pull-down** | `KEY1`~`KEY5` | **下降沿** |
| PC6~PC10 | 同上 | **No pull-up and no pull-down** | `KEY6`~`KEY10` | **下降沿** |

> - **不要配内部上拉**：硬件已有 1kΩ 串阻 + 10kΩ 上拉 + 10µF RC 消抖（原理图第 3 页）。
> - User Label 必须填 `KEY1`~`KEY10`（代码里要用 `KEY1_Pin`/`KEY1_GPIO_Port`）。
> - **触发沿必须是下降沿（`GPIO_MODE_IT_FALLING`）** —— 依据 2025 老从机固件（`gpio.c` 里
>   10 路全是 `GPIO_MODE_IT_FALLING` + `NOPULL`，整季验证过）。
>   ⚠️ **曾错误配成双边沿**：一次击打会产生 2 个沿（击打 + 回弹），第二个沿在"已击发"状态下到达，
>   会被判成"打错"→ 上报 `[2]=0x01` → **主机整轮复位**。
>   **本项已于验收时直接改好**（`.ioc` 的 10 处 `GPIO_ModeDefaultEXTI` + `gpio.c` 两处，保持
>   重新生成一致）；S4 实现击打判定时仍建议**再加一道软件窗口**（`HD_IGNORE_MS` 50~100ms，
>   忽略同一引脚在窗口内的后续沿）作为二道防线。

**③ System Core → NVIC**：使能（抢占优先级填 **3**）

| 中断 | 覆盖引脚 | 优先级 |
|---|---|---|
| `EXTI1_IRQn` ~ `EXTI4_IRQn` | PA1~PA4 | 3 / 0 |
| `EXTI9_5_IRQn` | **PA5 + PC6~PC9**（共用一条线） | 3 / 0 |
| `EXTI15_10_IRQn` | **PC10** | 3 / 0 |

> ⚠️ PA5 与 PC6~PC9 共用 `EXTI9_5`：回调里必须按 `GPIO_Pin` 分发（新代码 `hit_detect.c` 已按此设计）。

---

## 步骤 2：TIM3 加回 CH2 / CH4（CH1 保留）

**Timers → TIM3 → Mode**：

| 通道 | 设置 | 引脚 | 从机用途（老代码口径） |
|---|---|---|---|
| Channel1 | `PWM Generation CH1`（保持不动） | PA6 | `LED_ROAD_3` "上" |
| **Channel2** | **改成 `PWM Generation CH2`** | PA7 | `LED_ROAD_2` "中" |
| **Channel4** | **改成 `PWM Generation CH4`** | PB1 | `LED_ROAD_4` "灯臂" |

**参数页（Channel1/2/4 三个通道都要一致）**：

| 项 | 值 |
|---|---|
| Mode | PWM mode 1 |
| Pulse (CCR) | 0 |
| Output compare preload | Enable |
| Fast Mode | Disable |
| CH Polarity | High |

**时基页**（不动，与主机一致）：`Prescaler = 2`、`Counter Period = 34`
→ 84MHz/3/35 = **800kHz** 位率（T0H=10 / T1H=25 计数）。

---

## 步骤 3：TIM4 整个加回来（CH1/CH2/CH3）

**Timers → TIM4 → Activated ✅**，时基同上（`PSC=2`、`ARR=34`），三个通道：

| 通道 | Mode | 引脚 | 从机用途 |
|---|---|---|---|
| Channel1 | `PWM Generation CH1` | PB6 | `LED_ROAD_5` "侧灯臂"（120） |
| Channel2 | `PWM Generation CH2` | PB7 | `LED_ROAD_1` "下"（92） |
| Channel3 | `PWM Generation CH3` | PB8 | **老从机从未驱动** → 先配上，灯数待扫描确认 |

> PB8（TIM4_CH3）：老从机**从不驱动**该口 → 通道/DMA 配好但 `led_num` 填 `0u`（不分配缓冲、不发送）。
> 若日后确认该口接了灯带，只改 `WS_LED_NUM_SPARE` 一处（表与缓冲同源推导）。
> ⚠️ PB8 与 CAN1 的备选复用脚、BOARD_ID 候选冲突 —— 确认 CAN1 仍在 **PA11/PA12**（不要被自动分配到 PB8/PB9）。

---

## 步骤 4：DMA —— 5 条流（**方向是重中之重**）

**System Core → DMA → Add**，逐条添加：

| # | Request | CubeMX 自动分配 Stream | 对应通道 |
|---|---|---|---|
| 1 | `TIM3_CH2` | DMA1 Stream5 | PA7 |
| 2 | `TIM3_CH4/UP` | DMA1 Stream2 | PB1 |
| 3 | `TIM4_CH1` | DMA1 Stream0 | PB6 |
| 4 | `TIM4_CH2` | DMA1 Stream3 | PB7 |
| 5 | `TIM4_CH3` | DMA1 Stream7 | PB8 |
| （已有）| `TIM3_CH1/TRIG` | DMA1 Stream4 | PA6 |

**每条流的公共参数**（5 条全部相同）：

| 项 | 值 | 说明 |
|---|---|---|
| **Direction** | **Memory To Peripheral** | ⚠️⚠️ **配成 Peripheral To Memory 会：①灯全黑；②失控 DMA 沿 CCR 地址递增写穿 TIM3 寄存器块，砸坏 TIM4/TIM5/TIM6(1ms调度)/TIM7 → 调度失效** |
| Mode | **Normal** | 不要 Circular |
| Increment Address — Peripheral | **不勾** | 外设侧固定是 CCRx |
| Increment Address — Memory | **勾** | 内存侧递增 |
| Peripheral Data Width | **Half Word** | |
| Memory Data Width | **Half Word** | |
| FIFO Mode | **Disable** | |
| Priority | **Low** | 不抢 CAN/控制节拍 |

---

## 步骤 5：NVIC 汇总核对

**System Core → NVIC**，最终应为：

| 中断 | 抢占/子 | 归属 |
|---|---|---|
| `CAN1_RX0_IRQn` | 1 / 0 | 主从总线 |
| `TIM6_DAC_IRQn` | 2 / 0 | 1ms 调度 |
| `EXTI1~4_IRQn`、`EXTI9_5_IRQn`、`EXTI15_10_IRQn` | 3 / 0 | 10 路击打输入 |
| `DMA1_Stream0/2/3/4/5/7_IRQn` | 5 / 0 | 6 条灯带 DMA 完成回调 |
| `SysTick_IRQn` | 15 / 0 | HAL 时基 |

优先级分组保持 **4 bits for pre-emption**。

---

## 步骤 6：CAN（保持与主机一致，不要动）

| 项 | 值 | 说明 |
|---|---|---|
| CAN1 | **1Mbps**：`Prescaler=3`、`BS1=10TQ`、`BS2=3TQ` | 与主机一致（已由旧代码确认、主机对接测试通过） |
| CAN1 的 `AutoRetransmission` / `AutoBusOff` | 都 **Enabled** | 与主机一致；坏总线时可观测、能自愈 |
| **CAN2** | **保留不动** | ⚠️ 从机虽然不用电机总线，但 `bsp/bsp_can.c` 是**主从共用**的（同时初始化 CAN1+CAN2）；删了 CAN2 会编译不过。留着无害（无流量） |

---

## 步骤 7：时钟树核对

`HSE 8MHz → PLL /4 ×168 /2 → SYSCLK 168MHz`；`APB1 = 42MHz`（**APB1 Timer = 84MHz**）。
CAN 位率与 WS2812 时基都依赖这两个数，别被自动改动。

---

## 步骤 8：生成后的校验（逐条跑）

```bash
# ① 外设与 DMA
grep -E "^Mcu\.IP[0-9]" 2027_Embedded_Buff_Slave.ioc
#   期望: CAN1 CAN2 DMA NVIC RCC RNG SYS TIM3 TIM4 TIM6

grep -E "^Dma\.Request" 2027_Embedded_Buff_Slave.ioc
#   期望 6 条: TIM3_CH1/TRIG, TIM3_CH2, TIM3_CH4/UP, TIM4_CH1, TIM4_CH2, TIM4_CH3

# ② ★ 最关键: 6 条 DMA 方向全是 MEMORY_TO_PERIPH
grep -E "^Dma\..*Direction" 2027_Embedded_Buff_Slave.ioc
#   期望: 6 行, 全部 =DMA_MEMORY_TO_PERIPH

grep -c "Init.Direction = DMA_MEMORY_TO_PERIPH" Core/Src/tim.c
#   期望: 6

# ③ GPIO 复用(应含 PA6/PA7/PB1 于 TIM3, PB6/PB7/PB8 于 TIM4)
grep -A 8 "TIM3 GPIO Configuration" Core/Src/tim.c
grep -A 8 "TIM4 GPIO Configuration" Core/Src/tim.c

# ④ 中断入口(it.c): 6 个 EXTI + 6 个 DMA + CAN1_RX0 + TIM6_DAC + SysTick
grep -n "^void .*IRQHandler" Core/Src/stm32f4xx_it.c

# ⑤ 手写调度器仍在
grep -n "g_sched_tick_cnt\|HAL_TIM_PeriodElapsedCallback" Core/Src/stm32f4xx_it.c

# ⑥ KEY 宏已生成
grep -c "define KEY[0-9]*_Pin" Core/Inc/main.h      # 期望: 10
```

**编译**：此时代码侧（主机快照）能直接编过 —— `config_ws2812.h` 里目前只有 1 条灯带（TIM3_CH1），
其余通道等我把从机代码写好后再逐个填入。

---

## 步骤 9：改完把这些信息告诉我

| # | 信息 | 用途 |
|---|---|---|
| 1 | 上面 ② 的校验输出（6 行方向） | 确认没踩 DMA 方向坑 |
| 2 | 是否保留了 CAN2、是否删了别的外设 | 决定 `bsp_can.c` 要不要加角色分支 |
| 3 | PB8（TIM4_CH3）是否接灯带（先目视/万用表） | 决定灯带表是 5 条还是 6 条 |
| 4 | 从机板实物一共几块、每块对应灯板号（1~5） | 我把 `BUFF_NUM` 做成 config 宏（按板编译） |
| 5 | 每块从机板的灯带条数与大致布局 | 已按老从机口径继承（87/262/320/120/92 + PB8 不驱动）；只剩"哪条在机械上叫什么"待结构组确认(H5) |

之后我会写：`application/hit_detect.c`（EXTI 采集 + 环数查表 + armed/fired/locked/mishit）、
`application/buff.c` 的从机状态机（替换主机构建专用段）、`application/light.c` 的灯板图案库
（待激活/箭头流动/环数/进度 n/5/全激活）、以及 `config_ws2812.h` 的多灯带表 + 缓冲。
