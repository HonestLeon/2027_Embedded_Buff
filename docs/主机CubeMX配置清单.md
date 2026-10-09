# 主机（中心板）CubeMX 配置清单

> 目标 MCU：**STM32F405RGT6**（LQFP64）
> 用途：2026 赛季能量机关（大符/小符）固件重构，从零新建 .ioc
> 依据：`原理图/SCH_F405_2026-10-02.pdf`（4 页：主控 / 端子 / 硬件消抖 / 电源，V1.0，重绘日期 2026-01-26）
> 参考教程：`../../CubeMX基础配置.md`（含 `../../CubeMX基础配置.zip`；本清单沿用其操作步骤与截图，
> 截图存于 `../../图片和附件/`）—— 教程在仓库之外，本清单是唯一的工程内出处。
>
> ⚠️ **重要前提**：本原理图就是 2025 赛季旧代码运行的同一块 PCB（主从共用板，主从分别烧录）。
> 原理图核对时已与旧 master/slave 固件、旧 master `BUFF2024.ioc` 交叉验证。
> 旧 `.ioc` 的问题不是"外设配多了"，而是**失同步**：缺 CAN1、缺拨码开关 GPIO（代码里手工补的）。

---

## 目录

- [0. 原理图 ↔ 旧代码 交叉核对结果](#0-原理图--旧代码-交叉核对结果)
- [1. 新建工程](#1-新建工程)
- [2. System Core：RCC / SYS / DMA / NVIC](#2-system-corercc--sys--dma--nvic)
- [3. Clock Configuration：时钟树](#3-clock-configuration时钟树)
- [4. Timers：TIM6（1ms 节拍）/ TIM3 / TIM4（WS2812）](#4-timerstim6-1ms-节拍--tim3--tim4ws2812)
- [5. Connectivity：CAN1 / CAN2](#5-connectivitycan1--can2)
- [6. Multimedia / GPIO：RNG、拨码开关、ID、U15、KEY1~10](#6-multimedia--gpiorng拨码开关idu15key110)
- [7. Middleware：DSP 数学库](#7-middlewaredsp-数学库)
- [8. Project Manager](#8-project-manager)
- [9. 生成后需要手工补的东西](#9-生成后需要手工补的东西)
- [10. 本工程不使用的外设（避坑说明）](#10-本工程不使用的外设避坑说明)
- [11. 配置完成后的自检清单](#11-配置完成后的自检清单)
- [附录 A：主机引脚总表](#附录-a主机引脚总表)
- [附录 B：WS2812 时序计算](#附录-bws2812-时序计算)
- [附录 C：CAN 位率计算](#附录-ccan-位率计算)

---

## 0. 原理图 ↔ 旧代码 交叉核对结果

### 0.1 已确认（可直接照此配置）

| 项 | 结论 | 验证方式 |
|---|---|---|
| WS2812 输出 ×6 | TIM3_CH1/2/4 + TIM4_CH1/2/3（见第 4 节） | 旧 master .ioc 的 6 条 DMA 请求与原理图逐一吻合 |
| WS2812 时基 | TIM3/TIM4 = PSC=2, ARR=34 → **800kHz**，T0H=10(357ns)、T1H=25(893ns) | APB1 Timer=84MHz，旧板实测可用 |
| 灯带灯数 | **主机实接 1 条：中心 = TIM3_CH1(PA6) = 88 颗**（老固件 `LED_ROAD_3`；维护者注释“中心的颜色刷新不正常（对应 LED_ROAD_3）”）；从机 5 条：92/262/87/320/120 | 实测接线 + 旧 master/slave `WS2812.h` |
| CAN1 | PA11/PA12，U10=TJA1044GTK/3Z，VIO=3V3，120Ω 终端电阻带拨动开关 | 原理图 + 旧代码用 hcan1 |
| CAN2 | PB12/PB13，U11=TJA1044GTK/3Z | 原理图 + 旧 .ioc |
| 击打检测 | RDET1~10 经硬件 RC 消抖（1kΩ 串阻+10kΩ 上拉+10µF）→ KEY1~10 = **PA1~PA5、PC6~PC10** | 与旧 slave `main.h` RDET 引脚完全一致 |
| 拨码开关 | SW1 大/小符=**PD2**、SW2 扇叶停/转=**PB3**、SW3 红/蓝=**PB4**（均与旧代码一致）、SW4 调试模式=**PB5**（硬件有，旧固件未用） | 原理图导线 + 旧 main.c |
| HSE | **8MHz**（X1） | 原理图主控页 |
| 板卡身份 | ID1/ID2/ID3（10kΩ 上拉）= **PC11/PC12/PB9**，三档拨动开关选择拉低。硬件既有，旧固件未使用 | 原理图 |
| 电源 | 24V(XT30) → TPS54540 → 5V → SPX3819 → 3V3 | 原理图电源页 |

### 0.2 ✅ 丝印错位（已实测结案）

端子页 DIN 丝印编号与旧固件的灯带功能注释**对不上**（TIM↔MCU 引脚经导线核对无疑义，错位发生在"丝印命名"这一层）：

| TIM 通道 | 板上丝印 | 旧固件实际用途 | 实测接线（2026 通道扫描） |
|---|---|---|---|
| **TIM3_CH1 (PA6)** | DIN6（无中文标注） | 上灯带 | **★主机实接的"中心"灯带（88 颗）** |
| TIM3_CH2 (PA7) | DIN5 外灯臂 | 中灯带 | 主机未接 |
| TIM3_CH4 (PB1) | DIN3 下灯带 | 灯臂 | 主机未接 |
| TIM4_CH1 (PB6) | **DIN2 中心圆环** | 侧灯臂（老从机用） | 主机未接 —— **丝印写"中心圆环"但实物中心不在此** |
| TIM4_CH2 (PB7) | DIN4 内灯臂 | 下灯带 | 主机未接 |
| TIM4_CH3 (PB8) | DIN1 上灯带 | 主从固件均未启动过 | 主机未接（`led_num=0`） |

> ✅ **结论（2026 实测）**：主机板实接灯带**只有 1 条 —— "中心"，挂在 TIM3_CH1 / PA6 / 88 颗**
> （老固件 `LED_ROAD_3`，与维护者当年那条注释一致）。
> 板上丝印与实物接法确实错位：PA6 被标为 “DIN6”，而 “DIN2 中心圆环” 实际标在 PB6（空置端口）上。
> 其余 5 个通道全部未接灯带 → `led_num=0`（不分配缓冲/不发送，省 RAM）。
>
> **代码中一律用 TIM 通道名/索引**，不用上/中/下等称呼；定位方法（通道扫描 + 读变量）
> 已固化在 `light.c` 的白光测试模式中，换板/改线后可重跑。

### 0.3 待确认 / 已结案

**✅ 已结案（实测）**

- [x] **DIN 丝印错位 —— 已清理**：主机实接灯带只有 1 条“中心”，在 TIM3_CH1/PA6/88 颗；
      丝印把 PA6 标为 DIN6、把“中心圆环”标在 PB6（空置口）上（详见 0.2）
- [x] **TIM4_CH3 “上灯带”是否接灯带 —— 未接**（`led_num=0`）
- [x] **拨码极性 —— 实测 OFF/下=低、ON/上=高**；三个 `*_ACTIVE_LEVEL=1`，
      语义对齐旧固件（高=转动/蓝色/大符）

**⬜ 待确认**

- [x] **CAN 波特率 —— 已结案：CAN1 与 CAN2 均为 1Mbps**
      依据旧工程**生成代码**`can.c`（主/从机一致）：`Prescaler=3, BS1=10TQ, BS2=3TQ`
      → 42MHz/3/14 = 1Mbps。旧 `.ioc` 的 "CAN2=100kbps" 是陈旧值，勿采信。
      ⚠ 新工程 CAN2 曾按 100kbps 配置，须改为与 CAN1 相同（P=3/BS1=10/BS2=3）
- [ ] **U15 四针 wafer（WAFER-MX1.25-4PWB，3V3/GND + 2 信号线各 10kΩ 上拉）**：
      疑似《大符思路》记载的"3V3，GND，CLK，DIO"接口。所接 MCU 引脚在重绘原理图中
      无网标、无法解析（疑似 PC0/PC1 附近），旧固件也未使用。问硬件要原始工程确认
- [ ] **TJA1044 的 STB（pin8）接法**：接地=常开（正常）；若接 GPIO 需补配输出脚拉低
- [x] **灯带灯数复核 —— 已确认 88 颗**（目测整条全亮，无需修改）
- [ ] **电机侧台架标定**：小符模式实测圈时间（期望 6.0 s/圈 = π/3 rad/s）；
      若偏差则标定 `MOTOR_CIRCLE_NUM`（`新值 = 153.5 × 实测 ÷ 6.0`）

---

## 1. 新建工程

1. **Help → Manage Embedded Software Packages**：确认已安装 `STM32F4` 固件包（选最新 1.28.x）；
   同时勾选安装 **X-CUBE-ALGOBUILD**（第 7 节 DSP 库要用，没装的话这一步先装上）。
2. **File → New Project** → 搜索 `STM32F405RGT6` 双击选中。
3. 先做第 2、3 节的 RCC/SYS/时钟树，再配外设——时钟不对，后面所有波特率/时序计算都是错的。

> 操作习惯建议（来自参考教程）：在右上角引脚图上**切换英文输入法**后直接敲引脚名
> （如 `PA6`）或外设名（如 `can1`）可以快速定位可配置引脚；
> 中文输入法会重复输入两次导致搜索失败。
>
> ![引脚快速搜索](图片和附件/image%2015.png)

---

## 2. System Core：RCC / SYS / DMA / NVIC

### 2.1 RCC —— 开外部晶振

**System Core → RCC**：

| 项 | 值 |
|---|---|
| HSE | **Crystal/Ceramic Resonator**（本板 X1 为 8MHz 无源晶振，已确认） |
| LSE | Disable（不用 RTC） |

![RCC 选择 Crystal/Ceramic Resonator](图片和附件/image%202.png)

![RCC HSE/LSE 选项](图片和附件/image%2013.png)

> 教程原话：一般选择 Crystal/Ceramic Resonator 让时钟起振，否则外部时钟不会工作；
> 只用内部时钟才选 Disable。
>
> **常见坑（教程强调）**：只有同时在 RCC 开启外部晶振、**并且**在时钟树里把
> SYSCLK 源切到 HSE，外部时钟才真正生效；漏了第二步的话芯片仍在跑内部 HSI，
> 且"看起来一切正常"——直到你发现波特率/时序全偏。若代码启动异常、
> 波特率不对，先回来查这两步。

### 2.2 SYS —— 烧录方式

**System Core → SYS**：

| 项 | 值 |
|---|---|
| Debug | **Serial Wire** |
| Timebase Source | **SysTick**（保持默认） |

![SYS Debug 选择 Serial Wire](图片和附件/image%2021.png)

> 教程原话：**一定要记得选烧录方式，选 Disable 会使芯片"上锁"**（无法再 SWD 烧录）。
>
> 两个理由必须用 Serial Wire 而不是完整 JTAG：
> ① 本板 **PB3/PB4 是拨码开关输入**（JTDO/NJTRST），选 Serial Wire 自动释放它们；
> ② PA15/PB3/PB4 留给 GPIO。
>
> Timebase Source 保持 SysTick：HAL 的 `HAL_Delay()`/超时都基于它；
> 我们自己的 1ms 业务节拍用 TIM6（见 4.1），两者职责不同，不要混。

### 2.3 DMA —— 6 条 Stream（WS2812 专用）

**System Core → DMA → Add**，逐条添加 6 个请求：

| # | Request | 旧 .ioc 分配 Stream（可对照） |
|---|---|---|
| 1 | TIM3_CH1 | DMA1 Stream4 |
| 2 | TIM3_CH2 | DMA1 Stream5 |
| 3 | TIM3_CH4 | DMA1 Stream2 |
| 4 | TIM4_CH1 | DMA1 Stream0 |
| 5 | TIM4_CH2 | DMA1 Stream3 |
| 6 | TIM4_CH3 | DMA1 Stream7 |

每条 Stream 的参数（6 条全部相同）：

| 项 | 值 | 为什么 |
|---|---|---|
| **Direction** | **Memory To Peripheral** | ⚠⚠⚠ **最容易配错、后果最严重的一项**！选成 Peripheral To Memory 会导致：①`HAL_DMA_Start_IT` 把 PAR 当作源(=缓冲区, 但 PINC=0 → 永远只读 buffer[0])、M0AR 当作目标(=CCR1, MINC=1 → 写地址每 +2 递增)，**一路冲出 TIM3 寄存器块**，把 TIM4/TIM5/TIM6/TIM7 寄存器全扫一遍（**1ms 调度定时器 TIM6 也被写入垃圾**）→ 灯全黑 + 调度失效；②铁证：`TIM3->DMAR` 会被写入非法值（我们的代码从不写这个寄存器）。
判据：以**能工作的老工程生成代码**为准（`DMA_MEMORY_TO_PERIPH`）；注意 `.ioc` 里该字段的历史值与生成代码可能不一致，别只看 ioc |
| Mode | **Normal** | 一帧一停；Circular 会让 WS2812 不停重发旧帧。完成回调里 Stop + 清 busy（9.2） |
| Increment Address - Peripheral | **不勾** | 外设地址固定是 CCRx 寄存器 |
| Increment Address - Memory | **勾** | 缓冲区逐个搬运 |
| Peripheral Data Width | **Half Word** | CCRx 是 16 位有效 |
| Memory Data Width | **Half Word** | 与外设一致，避免格式转换 |
| FIFO Mode | **Disable** | 直接模式足够 |
| Priority | **Low** | 灯效允许晚一帧，不能抢 CAN/控制节拍（旧工程全 VeryHigh 无必要） |

> 注：DMA 配置项在 Timers 里启用了 PWM 的 DMA 之后也会出现在 TIM 配置页内，
> 两处是同一份配置，改哪边都行，但**别两边改成不一样的**。

### 2.4 NVIC —— 中断优先级

**System Core → NVIC**，先把页面顶部的优先级分组定为：

| 项 | 值 |
|---|---|
| Priority Group | **4 bits for pre-emption**（`NVIC_PRIORITYGROUP_4`，全抢占） |

然后按下面的表填（在对应外设使能中断后，NVIC 页会自动出现这些行）：

| 中断 | Pre-emption | Sub | 说明 |
|---|---|---|---|
| CAN1_RX0_IRQn | **1** | 0 | 主从互联，最高业务优先级：1Mbps 下帧间隔 111µs |
| CAN2_RX0_IRQn | **1** | 1 | 电机反馈 |
| TIM6_DAC_IRQn | **2** | 0 | 1ms 节拍，中断内只置标志 |
| EXTI1_IRQn ~ EXTI4_IRQn | **3** | 0 | KEY1~4 |
| EXTI9_5_IRQn | **3** | 0 | KEY5(PA5) + KEY6~9(PC6~9)，回调内按 GPIO_Pin 分发 |
| EXTI15_10_IRQn | **3** | 0 | KEY10(PC10) |
| DMA1 Stream0/2/3/4/5/7 | **5** | 0 | WS2812 传输完成回调 |
| SysTick | **15** | 0 | HAL 时基，最低 |
| RNG_IRQn | 默认 | — | 上电取一次种子 |
| ~~TIM2_IRQn~~ | — | — | **不启用**（旧 .ioc 的 TIM2 专为喂随机数，已删除，改用 RNG） |

优先级设计原则：**CAN > 控制节拍 > 击打 EXTI > DMA 回调 > SysTick**。
（EXTI 放 3：击打信号已经过 RC 硬件消抖，毫秒级延迟不敏感，但不能被 DMA 长时间阻塞。）

---

## 3. Clock Configuration：时钟树

**打开 Clock Configuration 页**，按顺序从左往右配：

1. **Input frequency = 8**（本板晶振 8MHz，按实物填！教程强调：C 板是 12MHz，
   大部分自制板是 8MHz，填错外部时钟照样能跑但一切频率都是错的）；
2. PLL Source Mux 选 **HSE**；
3. **/M = 4**（8/4 = 2MHz，落在 PLL 输入 1~2MHz 规范内）；
4. **×N = 168**（2×168 = 336MHz VCO，规范 100~432MHz 内）；
5. **/P = 2** → SYSCLK = **168MHz**；
6. SYSCLK Mux 选 **PLLCLK**；
7. AHB = /1 → **HCLK = 168MHz**；
8. APB1 = /4 → **42MHz**；APB2 = /2 → **84MHz**。

![时钟树默认状态（HSE 灰色）](图片和附件/image%2040.png)

![时钟树配置顺序：input frequency → HSE → HCLK](图片和附件/image%205.png)

![时钟树配置完成参考](图片和附件/image%2019.png)

**填完后的检查点（必查）**：

| 检查项 | 期望值 |
|---|---|
| 页面有无红色/蓝色告警 | 无（CubeMX 会把非法组合标红并自动改值，改了就要查原因） |
| HCLK | 168MHz |
| **APB1 Timer clocks** | **84MHz** ← WS2812 时基与 TIM6 分频全靠它 |
| APB1 peripheral clocks | 42MHz ← CAN 位率计算用它 |
| To Cortex System timer | 168MHz |

> 原理图核对过的换算链：HSE 8MHz → /4 → ×168 → /2 = 168MHz；
> APB1 分频 ≠1 时定时器时钟自动 ×2，所以 TIM3/TIM4/TIM6 拿到的是 **84MHz** 而不是 42MHz。

---

## 4. Timers：TIM6（1ms 节拍）/ TIM3 / TIM4（WS2812）

### 4.1 TIM6 —— 1ms 业务节拍（新增，整个调度框架的心脏）

**Timers → TIM6**，Mode 里勾 **Activated**，参数：

| 项 | 值 | 计算 |
|---|---|---|
| Prescaler | **83** | 84MHz(APB1 Timer) / (83+1) = 1MHz 计数频率 |
| Counter Period | **999** | 1MHz / (999+1) = 1kHz → 中断周期 1ms |
| auto-reload preload | Enable | 计数稳定 |

**NVIC 页**勾选 TIM6 全局中断（抢占 2，见 2.4）。

> 用途：主循环的固定节拍——1ms 电机控制环、250ms（4Hz）逻辑帧、2.5s/1s 超时
> 全部由这个 tick 计数驱动，替代旧代码里 SysTick 塞计数 + 三套各写各的时间累加。
> 中断服务函数里**只置一个 volatile 标志**，所有业务留在主循环。
>
> 注意：**不要**把它设为 HAL Timebase（2.2 节已把 Timebase Source 定为 SysTick）。

### 4.2 TIM3 —— WS2812 PWM（CH1/CH2/CH4 三路）

**Timers → TIM3**：

① Clock Source 选 **Internal Clock**；

② 时基参数（与 TIM4 必须完全一致）：

| 项 | 值 | 说明 |
|---|---|---|
| Prescaler (PSC) | **2** | 84MHz / (2+1) = 28MHz 计数频率 |
| Counter Period (ARR) | **34** | 28MHz / (34+1) = **800kHz** = WS2812 标准 bit 率 |
| auto-reload preload | Enable | |

③ 通道模式：把 **CH1、CH2、CH4** 三项都选 **PWM Generation CHx**：

| 通道 | 引脚 | 旧固件用途 |
|---|---|---|
| TIM3_CH1 | PA6（自动） | 上灯带（主机 88 灯 / 从机 87 灯） |
| TIM3_CH2 | PA7（自动） | 中灯带（262 灯） |
| TIM3_CH4 | PB1（自动） | 灯臂（主机 256 / 从机 320 灯） |

④ 每路的 PWM 参数（在下方 Parameter Settings）：

| 项 | 值 |
|---|---|
| Mode | PWM mode 1 |
| Pulse (CCR) | 0 |
| Output compare preload | Enable |
| Fast Mode | Disable |
| CH Polarity | High |

⑤ 回到 **DMA Settings**（或第 2.3 节添加的条目），确认 CH1/CH2/CH4 三条 DMA
参数与 2.3 节一致；并**勾选 NVIC 里的 DMA 流中断**（抢占 5，见 2.4）——
完成回调 `HAL_TIM_PWM_PulseFinishedCallback` 依赖它。

> 时基备选方案：PSC=0, ARR=104（同为 800kHz，占空比分辨率 ×5），
> 但 T0H/T1H 计数值需按附录 B 重新折算并实测——**求稳沿用旧板实测参数**。

### 4.3 TIM4 —— WS2812 PWM（CH1/CH2/CH3 三路）

**Timers → TIM4**，配置方法与 TIM3 完全相同（PSC=2, ARR=34）：

| 通道 | 引脚 | 旧固件用途 |
|---|---|---|
| TIM4_CH1 | PB6（自动） | 侧灯臂（从机专用，120 灯） |
| TIM4_CH2 | PB7（自动） | 下灯带（92 灯） |
| TIM4_CH3 | PB8（自动） | **旧固件从未启动；实测主机未接灯带**（`led_num=0`，不分配缓冲） |

> 6 路全部配置（与旧 master .ioc 的 6 条 DMA 一致）。即使暂时只用 5 路，
> 配置齐了固件按需启动，避免以后补配。
>
> ⚠️ **PB8 引脚冲突预警**：PB8 同时是 CAN1 的备选复用引脚之一（见 5.1 的坑）
> 和 BOARD_ID3 的旧规划候选——本清单最终定为：PB8=TIM4_CH3、
> ID3=PB9、CAN1 用 PA11/PA12，三者不冲突，配置时注意别被自动分配带偏。

---

## 5. Connectivity：CAN1 / CAN2

### 5.1 CAN1 —— 主从板互联（收灯板 0x160，发 0x161）

**Connectivity → CAN1 → Activated** ✅（旧 .ioc 缺 CAN1 属于失同步，硬件一直有 U10 收发器）。

**① 先确认引脚是 PA11/PA12**：

> **坑（参考教程同款）**：CubeMX 给 CAN1 默认分配的引脚不一定是 PA11/PA12，
> C 板教程里默认给的是 PB8/PB9、要手动切到 PD0/PD1。
> 本板原理图是 **PA11=CAN1_RX、PA12=CAN1_TX**（U10 TJA1044GTK/3Z），
> 而且我们的 PB8/PB9 已被 TIM4_CH3 和 BOARD_ID3 占用——
> 配完CAN1后必须到引脚图上确认 CAN1 落在 PA11/PA12，
> 若不对就在芯片图上直接点击 PA11 选 `CAN1_RX`、PA12 选 `CAN1_TX`。
>
> ![CAN 默认引脚可能不对](图片和附件/image%208.png)
>
> ![确认实际使用的 CAN 引脚](图片和附件/image%2016.png)

**② 位率参数**（Parameter Settings）：

计算公式（教程给出）：**波特率 = PCLK1 / Prescaler / (1 + Tq1 + Tq2)**，其中 PCLK1 = APB1 = 42MHz。

先配 Tq1/Tq2，最后调 Prescaler（教程提醒：顺序反了容易因软件求解异常报错）：

| 项 | 值 | 验算 |
|---|---|---|
| Prescaler | **3** | 42MHz / 3 = 14MHz → Tq = 71.4ns |
| Time Quanta in Bit Segment 1 | **10** | |
| Time Quanta in Bit Segment 2 | **3** | |
| 波特率 | **1Mbps** | 42/3/(1+10+3) = 42/3/14 = 1.000MHz ✅ |
| 采样点 | 78.6% | (1+10)/14 |
| ReSynchronization Jump Width | 1 | |

更多 1Mbps 组合见附录 C；**CAN1 波特率已由旧从机生成代码确认（1Mbps）**，见 0.3 结案项。

![CAN 位率参数计算页面](图片和附件/image%2022.png)

![CAN 位率参数填写](图片和附件/image.png)

![先配 Tq 再配 Prescaler 的操作顺序](图片和附件/image%2041.png)

**③ 中断**：NVIC 页勾选 **CAN1 RX0 interrupt**（抢占 1，子 0）。

> 教程说"一般需要用到所有的 FIFO，所以 RX0 和 RX1 的接收中断都需要开启"；
> 本工程过滤器全部指向 FIFO0（见 9.3），所以**只需 RX0**。
> 若你把过滤器分配到了 FIFO1，才需要把 RX1 也勾上。

![CAN 接收中断使能](图片和附件/image%2039.png)

### 5.2 CAN2 —— 电机控制总线（发 0x200，收 0x201~0x204）

**Connectivity → CAN2 → Activated** ✅，引脚 **PB12=CAN2_RX、PB13=CAN2_TX**（与旧 .ioc 一致）。

**位率参数：1Mbps（已结案）**——与 CAN1 完全相同：

| 参数 | 值 | 验算 |
|---|---|---|
| Prescaler | **3** | 42M/3 = 14MHz → Tq = 71.4ns |
| Time Quanta in Bit Segment 1 | **10** | |
| Time Quanta in Bit Segment 2 | **3** | 位时间 = (1+10+3)×Tq = 1.000µs |
| 波特率 | **1Mbps** | 42/3/14 = 1.000MHz ✅ |
| 采样点 | 78.6% | (1+10)/14 |

**依据（重要）**：**旧工程生成代码**（非 `.ioc`）——旧主机与旧从机的 `Core/Src/can.c` 里
CAN1 与 CAN2 **都是** `Prescaler=3 / BS1=10TQ / BS2=3TQ` → **1Mbps**。
旧 `.ioc` 写的 “CAN2=100kbps” 是陈旧值（同一 ioc 连 CAN1 条目都缺失，而代码在用 `hcan1`），
**不可采信**。总线容量核算：8 字节帧 ≈110~135µs，1kHz 发帧 ≈13%，反馈 1kHz 再 ≈13% → 总 ≈26%，余量充足。

> ⚠️ **若新工程曾按 100kbps 配（P=28/BS1=11/BS2=3），必须改回上表值**——
> 否则电机完全不响应（帧被总线当作错误丢弃）。

NVIC：**CAN2 RX0 interrupt**（抢占 1，子 1）。

> TJA1044 的 **STB 引脚**接法待确认（0.3）：若接地则收发器常开，无需软件干预；
> 若接了 MCU GPIO，需在 CubeMX 补 2 个 GPIO_Output 并初始化拉低，否则 CAN 无输出。

---

## 6. Multimedia / GPIO：RNG、拨码开关、ID、U15、KEY1~10

### 6.1 RNG —— 硬件随机数

**Multimedia → RNG → Activated**。

> 硬件一直有，旧固件没用（旧代码 `srand(1)`/`srand(CNT_my)`/TIM2 中断喂种子三套混用）。
> 上电 `HAL_RNG_GenerateRandomNumber()` 读一次做 `srand()` 种子即可。
> NVIC 保持默认（RNG_IRQn 无需抢占优先级调高）。

### 6.2 拨码开关 ×4（GPIO Input）

在芯片图上点击引脚选 **GPIO_Input**，再到 **System Core → GPIO** 页配置：

| 信号 | 引脚 | GPIO mode | Pull | User Label | 功能 | 旧固件 |
|---|---|---|---|---|---|---|
| SW1 | **PD2** | Input | Pull-up | `SW_BUFF` | 大符/小符：**高=大符，低=小符** | ✅在用 |
| SW2 | **PB3** | Input | Pull-up | `SW_RUN` | 扇叶停/转：**高=转（ON），低=停（OFF）** | ✅在用 |
| SW3 | **PB4** | Input | Pull-up | `SW_COLOR` | 红/蓝：**高=蓝，低=红**（与旧代码一致） | ✅在用 |

> ⚠ **极性已实测确定**（2026 上板验证）：开关 **OFF/下 → 引脚低电平**，**ON/上 → 高电平**
> （内部上拉，OFF 时开关把引脚拉到 GND）。因此 `config_board.h` 里三个 `*_ACTIVE_LEVEL` 均为 **1**，
> 语义对齐旧固件：高电平 = 转动 / 蓝色 / 大符。
> 曾因按旧注释假设“低电平有效”导致**测试模式与红蓝颜色双双反向**，已修正。
| ~~SW4~~ | **PB5** | Input | Pull-up | `SW_DEBUG` | ⚠ **原理图有、实物未装配**（旧代码也只定义 SW[3]）：CubeMX 保留该脚为输入备用，**代码不读取**；下一版装上开关即可用 |

> ① **User Label 一定要填**：生成 `SW_BUFF_Pin`/`SW_BUFF_GPIO_Port` 宏，
> 代码里不再出现裸的 `GPIO_PIN_4`（旧代码可读性差的一大来源）。
> ② PB3/PB4 即 JTDO/NJTRST，2.2 节 Debug 选了 Serial Wire 后自动释放为 GPIO。
> ③ 上/下拉先按内部上拉配；**上板后用万用表核对挡位↔电平真值表**（0.3），
> 因为原理图上没看到开关信号的外部上拉电阻。

### 6.3 ID 身份选择 ×3（主从共板角色识别）

| 信号 | 引脚 | GPIO mode | GPIO Pull | User Label | 说明 |
|---|---|---|---|---|---|
| ID1 | **PC11** | **GPIO_Input** | **No pull-up and no pull-down** | `BOARD_ID1` | 外部 10kΩ 已上拉 |
| ID2 | **PC12** | **GPIO_Input** | **No pull-up and no pull-down** | `BOARD_ID2` | 同上 |
| ID3 | **PB9** | **GPIO_Input** | **No pull-up and no pull-down** | `BOARD_ID3` | 同上（三档拨动开关把其中一路拉到 GND） |

> 三个脚都是**输入**（读板上拨动开关的电平来识别主/从角色，不做输出），
> mode 在芯片图上点击引脚选 `GPIO_Input`。
> Pull 选下拉框里的 **No pull-up and no pull-down**：外部已有 10kΩ 上拉，
> 电平由"开关是否拉低"决定——开关断开读 1、闭合读 0；
> 若再配内部上拉，开关断开时仍是 1 没问题，但闭合时会形成内部上拉↔开关对地的额外通路，
> 虽然一般不会损坏，但没必要，保持与外部电路设计一致即可。

> 硬件既有、旧固件未用。重构后用途：上电自检、主从固件合一按角色裁剪
> （读 BOARD_ID1~3 → 判定主机/从机 → 使能对应业务）。

### 6.4 KEY1~10 —— 击打检测（EXTI）

RDET1~10 端子 → 硬件 RC 消抖（1kΩ + 10kΩ + 10µF）→ KEY1~10 进 MCU。
**硬件已消抖，软件只需边沿检测，不要再写软件消抖。**

在芯片图上逐个点击引脚选 **GPIO_EXTIx**，GPIO 页配置：

| 信号 | 引脚 | GPIO mode | GPIO Pull | NVIC 中断 |
|---|---|---|---|---|
| KEY1~4 | PA1~PA4 | GPIO_EXTI1~4 | No pull（外部 10kΩ 上拉） | EXTI1~4_IRQn |
| KEY5 | PA5 | GPIO_EXTI5 | No pull | EXTI9_5_IRQn |
| KEY6~9 | PC6~PC9 | GPIO_EXTI6~9 | No pull | EXTI9_5_IRQn |
| KEY10 | PC10 | GPIO_EXTI10 | No pull | EXTI15_10_IRQn |

EXTI 触发沿：**从机构建必须配 `Falling`（下降沿）** —— 依据 2025 老从机固件
（10 路全是 `GPIO_MODE_IT_FALLING`，整季验证）；配成双边沿会导致一次击打产生两个沿，
第二个沿会被判"打错"并复位整轮（详见《从机重构清单》S4 硬约束）。
主机侧这 10 路**不做击打检测**（保留配置备用），双边沿/单边沿都不影响功能。

> 两个注意点：
> ① PA5 与 PC6~PC9 共用 **EXTI9_5** 一条中断线，PC10 在 **EXTI15_10**——
> 回调 `HAL_GPIO_EXTI_Callback` 里必须按 `GPIO_Pin` 分发（旧从机代码就是这么干的，可参考）；
> ② 引脚与旧从机 `main.h` 的 RDET1~10 完全一致，**环数↔引脚标定表可以直接平移**
> （RDET1→10环 … RDET5→6环，重绘原理图未改动这部分硬件）。
> NVIC 优先级抢占 3（见 2.4）。

### 6.5 U15 中心屏接口（待确认后补配）

> U15 = WAFER-MX1.25-4PWB，线序疑似 3V3/GND/**CLK/DIO**（与《大符思路》记载一致），
> CLK/DIO 各有 10kΩ 上拉。所接 MCU 引脚**待硬件确认**（疑似 PC0/PC1）。
> 确认后在 CubeMX 补 2 个 GPIO_Output（Output Push Pull，初始 Output level = High），
> 软件做位操作时序驱动；若最终换其他协议芯片，按其数据手册再定。

---

## 7. Middleware：DSP 数学库

旧主机代码用 `arm_math.h` 的 `arm_sin_f32()` 做大符正弦变速的前馈计算，
重构后电机控制仍会用到，**必须在 CubeMX 里启用 DSP 库**（否则要手动拷 CMSIS-DSP 源码）。

**Middleware and Software Packs → X-CUBE-ALGOBUILD**（带齿轮图标）：

1. 勾选 **DSP Library**；
2. 勾选后齿轮图标会消失，**记得再确认 DSP Library 已处于启用（勾选）状态**。

![点击带齿轮的 X-CUBE-ALGOBUILD](图片和附件/image%209.png)

![勾选 DSP Library](图片和附件/image%2017.png)

![确认 DSP Library 启用](图片和附件/image%2035.png)

![DSP 库启用成功状态](图片和附件/image%2027.png)

> 启用后工程会自动包含 CMSIS-DSP 头文件路径与库文件，代码里
> `#include "arm_math.h"` 即可；Keil 下还会自动加 `arm_cortexM4lf_math.lib`
> （旧工程里那个孤零零的 `.lib` 就是这么来的，新工程不需要手工拷）。

---

## 8. Project Manager

### 8.1 Project 页

| 项 | 值 |
|---|---|
| Toolchain / IDE | MDK-ARM（保守）或 **CMake**（推荐，摆脱 Keil） |
| Min Heap Size | 0x200 |
| Min Stack Size | 0x800 |

### 8.2 Code Generator 页（容易忽略但很重要）

| 项 | 值 | 为什么 |
|---|---|---|
| STM32Cube MCU packages | Copy only the necessary files | 仓库不臃肿 |
| **Generate peripheral initialization as a pair of '.c/.h' files** | ✅ **必勾** | `can.c/.h`、`tim.c/.h` 分文件，git diff 清晰 |
| Keep User Code when re-generating code | ✅ | 重生成不丢手写代码 |
| Delete previously generated files | 按喜好 | 新工程无历史包袱 |

> 有了 "pair of .c/.h"，所有手写代码放进 `/* USER CODE BEGIN x */ ... END */`
> 区域就永远不会被重新生成覆盖——这是防止旧工程那种"代码与配置两张皮"的关键。

### 8.3 Advanced Settings 页

| 项 | 值 |
|---|---|
| 各外设 Driver | HAL（默认） |

---

## 9. 生成后需要手工补的东西（CubeMX 之外）

### 9.1 WS2812 缓冲区与 RAM 预算

- 每条灯带 DMA 缓冲 = `LED_NUM × 24 + RESET_BITS(建议 90)`，`uint16_t`。
- **实测主机只接 1 条**：中心 = `TIM3_CH1(PA6)` = 88 颗 →
  `(88×24+90)×2B ≈ 4.4KB`（其余 5 通道 `led_num=0` 不分配缓冲）；
  实测**主机只接 1 条**（中心/PA6/88 颗）→ 静态缓冲仅 ≈4.4KB（其余 5 通道 `led_num=0` 不分配）。
- **缓冲必须放主 SRAM——DMA 不能访问 CCM（64KB 那块）**。
- 从机侧参考（同一份 bsp 代码）：881 灯 ≈ 43KB。
- RESET_BITS 建议 90：90 bit × 1.25µs = 112µs > WS2812 规格的 >50µs，
  旧工程 50 计数（62.5µs）偏紧，顺手加裕量。

### 9.2 WS2812 发送状态机（bsp 层）

```
rgb 帧缓冲（逻辑层随便改）
   ↓ 标记 dirty
发送任务检查: !dma_busy && dirty → RGB→PWM 翻译 → HAL_TIM_PWM_Start_DMA → busy=1
   ↓ DMA 完成中断
HAL_TIM_PWM_PulseFinishedCallback → Stop DMA → busy=0
```

旧代码每圈无条件重启 5 路 DMA（撞上未完成的传输就花屏），这是 R 标颜色异常的病根。

### 9.3 CAN 过滤器（bsp 层）

- MASK 模式只收 `0x160~0x161`，全部指向 FIFO0；
- 删除旧 `can_config.h` 里摩擦轮/拨弹/Yaw 等从别的项目拷来的遗留；
- RX 中断里只做 `HAL_CAN_GetRxMessage` + 入队，解码放主循环。

### 9.4 逐路点亮测试模式（先于一切业务逻辑）

```
上电 → SW2=ON 进入 → 遍历**本机实接灯带**逐条白光 2 秒
（主机只有“中心”1 条 → 表现为该条持续白光；未接通道自动跳过，不空转黑场）
```

用于解决 0.2 节的丝印错位问题，建立"功能名 ↔ TIM 通道"权威对照表。

### 9.5 上电时序（写进 main.c 的 USER CODE 2）

```
外设初始化 → HAL_Delay(100)（等 WS2812 供电稳）→ 读 ID/拨码 → 刷初始灯效 → CAN Start
```

旧 bug"先改开关再通电 R 标显示紫色"与此窗口相关。

---

## 10. 本工程不使用的外设（避坑说明）

参考教程里还讲了 USART-DMA、USB、FreeRTOS，本工程**都不用**，理由留档防止后人手痒：

| 外设 | 结论 | 理由 |
|---|---|---|
| USART/DMA | 不配 | 板上无串口器件；调试用 SWD + 运行时变量观察即可 |
| USB_DEVICE | 不配 | 无上位机通信需求 |
| FreeRTOS | 不配 | 负载 <10% CPU，1ms 控制环 + 4Hz 逻辑帧用 TIM6 置标志 + 主循环调度即可；
  上 RTOS 反而引入 RAM/优先级/DMA 并发新坑（详细论证见重构讨论记录） |
| TIM2 | **删除** | 旧 .ioc 里专为 5Hz 中断喂随机数存在，RNG 替代 |
| DAC/ADC/I2C/SPI | 不配 | 原理图未使用（U15 若确认为 CLK/DIO 位驱动也只需 GPIO） |

---

## 11. 配置完成后的自检清单

- [ ] Clock 树无告警：HSE=8MHz、SYSCLK=168MHz、**APB1 Timer=84MHz**
- [ ] RCC 开了 Crystal 且时钟树源选了 HSE（教程强调的双重检查）
- [ ] SYS Debug = Serial Wire（否则芯片上锁）
- [ ] TIM3/TIM4 六路 PSC=2、ARR=34 完全一致
- [ ] 逻辑分析仪：bit 周期 1.25µs（800kHz），T0H≈357ns，T1H≈893ns
- [ ] CAN1 落在 PA11/PA12（不是默认的 PB8/PB9！），位率验算 = 42/P/(1+Tq1+Tq2)
- [ ] CAN1/CAN2 波特率与实车一致（**已知：均为 1Mbps**，上板用 CAN 盒复核一次）
- [ ] DMA 六条 Stream：Normal 模式、Half Word、外设不递增/内存递增
- [ ] NVIC 分组 = 4 bits，优先级表按 2.4 填写
- [ ] 拨码真值表、ID 三挡读数确认
- [ ] KEY1~10 模拟击打触发正常、无抖动重复触发
- [ ] U15 用途与引脚已向硬件确认；TJA1044 STB 接法已确认
- [ ] DSP 库已勾选且 `arm_math.h` 编译通过
- [ ] 灯带点亮测试通过（中心灯带纯白、整条全亮），“中心=索引0/PA6/88颗”已回填（0.2 节）
- [ ] RAM 余量 ≥ 40KB（map 文件确认，WS2812 缓冲在主 SRAM）

---

## 附录 A：主机引脚总表

| 引脚 | 功能 | 说明 |
|---|---|---|
| PA1~PA5 | KEY1~5（EXTI） | = 旧从机 RDET1~5 |
| PA6 | TIM3_CH1 | 上灯带（旧固件口径） |
| PA7 | TIM3_CH2 | 中灯带 |
| PA11/PA12 | CAN1 RX/TX | U10 TJA1044 |
| PA13/PA14 | SWDIO/SWCLK | 调试口（Serial Wire） |
| PB1 | TIM3_CH4 | 灯臂 |
| PB3 / PB4 | GPIO Input | SW2 停转 / SW3 红蓝 |
| PB5 | GPIO Input（保留未用） | 原理图画了 SW4 调试模式，**实物未装配**；代码不读取 |
| PB6 | TIM4_CH1 | 侧灯臂（从机口径，120 灯） |
| PB7 | TIM4_CH2 | 下灯带 |
| PB8 | TIM4_CH3 | 丝印"上灯带"，旧固件未启动，**实测主机未接灯带** |
| PB9 | GPIO Input | BOARD_ID3（旧固件未用） |
| PB12/PB13 | CAN2 RX/TX | U11 TJA1044 |
| PC0/PC1 | （待确认） | 疑似 U15 CLK/DIO |
| PC6~PC10 | KEY6~10（EXTI） | = 旧从机 RDET6~10 |
| PC11/PC12 | GPIO Input | BOARD_ID1/2（旧固件未用） |
| PD2 | GPIO Input | SW1 大小符 |
| PH0/PH1 | HSE 8MHz | — |

## 附录 B：WS2812 时序计算

```
定时器计数频率 = APB1 Timer clocks / (PSC+1) = 84MHz / 3 = 28MHz
bit 率        = 计数频率 / (ARR+1)           = 28MHz / 35 = 800kHz（bit 周期 1.25µs）
T0H           = 10 × (1/28MHz) = 357ns（规格 0±150ns 容差内 ✅）
T1H           = 25 × (1/28MHz) = 893ns（规格 0.8µs±150ns ✅）
Reset 段      = RESET_BITS × 1.25µs，建议 90 计数 = 112µs（>50µs 规格要求）
```

若改用备选方案 PSC=0/ARR=104（tick=1/84MHz≈11.9ns）：
`T0H ≈ 34 计数(0.4µs)`、`T1H ≈ 67 计数(0.8µs)`，需逻辑分析仪实测微调。

## 附录 C：CAN 位率计算（PCLK1 = 42MHz）

公式：**波特率 = 42MHz / Prescaler / (1 + Tq1 + Tq2)**，采样点 = (1+Tq1)/(1+Tq1+Tq2)，推荐 75%~87.5%。

| 波特率 | Prescaler | Tq1 | Tq2 | 验算 | 采样点 |
|---|---|---|---|---|---|
| 1Mbps ✅ | 3 | 10 | 3 | 42/3/14 = 1.000M | 78.6% |
| 1Mbps | 6 | 4 | 2 | 42/6/7 = 1.000M | 71.4% |
| 1Mbps | 2 | 15 | 5 | 42/2/21 = 1.000M | 76.2% |
| 500kbps | 6 | 10 | 3 | 42/6/14 = 0.500M | 78.6% |
| 100kbps（旧 CAN2 改进） | 28 | 11 | 3 | 42/28/15 = 0.100M | 80.0% |
| 100kbps（旧 ioc 原值） | 42 | 5 | 4 | 42/42/10 = 0.100M | 60.0%（偏低） |

> 教程中的例子 42/6/(1+3+3)=1M 与 42/3/(1+6+7)=1M 验算同样成立，但采样点分别只有
> 57% 和 50%，不建议照抄——采样点过低在长总线/线缆质量差时易出错帧。
