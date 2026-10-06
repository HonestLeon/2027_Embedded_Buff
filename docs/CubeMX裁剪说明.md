# CubeMX 配置裁剪说明（主机固件 · P3 收尾）

> 目的：把主机固件**用不到**的定时器通道、DMA 流、中断、引脚从 CubeMX 里删干净。
> 依据：实测确认主机板**只有 1 条灯带**（"中心"，TIM3_CH1/PA6/88 颗），
> 主机构建**不做击打检测**（那是从机的职责，从机经 CAN 上报）。
>
> ⚠️ **操作前先备份**：把整个工程目录复制一份，或在 CubeMX 里 `File → Save` 前先提交 git。
> ⚠️ **前提检查**：`.ioc` 里 6 条 DMA 的 Direction 必须已经是 `DMA_MEMORY_TO_PERIPH`
> （已确认 ✓）——否则重新生成会把"灯带不亮"的 bug 带回来。

---

## 一、总览：删什么 / 留什么

| 类别 | 现在 | 裁剪后 | 删除的具体项 |
|---|---|---|---|
| 外设 | CAN1, CAN2, DMA, NVIC, RCC, RNG, SYS, **TIM3, TIM4**, TIM6 | 去掉 **TIM4** | TIM4 整个外设 |
| PWM 通道 | TIM3_CH1/CH2/CH4 + TIM4_CH1/CH2/CH3 = 6 | **1** | TIM3_CH2、TIM3_CH4、TIM4 全部 3 个通道 |
| DMA 流 | 6 条 | **1 条** | TIM3_CH2、TIM3_CH4、TIM4_CH1、TIM4_CH2、TIM4_CH3 的流 |
| NVIC 中断 | 13 个 | **5 个** | 5 个 DMA 流中断 + 6 个 EXTI 中断 |
| 占用引脚 | 30 个 | **14 个** | PA1~PA5、PC6~PC10、PB5、PB6、PB7、PB8、PB1、PA7 |

**裁剪后必须保留的（一个都不能删）**

| 保留项 | 用途 |
|---|---|
| **TIM3 的 CH1（PA6）** | 中心灯带（唯一实接灯带） |
| **TIM6** | 1ms 调度节拍（整个架构的心脏） |
| **CAN1（PA11/PA12）** | 从机总线（0x160/0x161） |
| **CAN2（PB12/PB13）** | 电机总线（0x200/0x201~204） |
| **RNG** | 上电随机种子 |
| **DMA 外设本身 + DMA1_Stream4** | TIM3_CH1 的波形搬运 |
| **PD2 / PB3 / PB4** | 三个拨码（大小符 / 停转 / 红蓝） |
| **PC11 / PC12 / PB9** | 板卡 ID1/ID2/ID3（从机 ID 也靠它） |
| **PA13 / PA14** | SWD 调试口 |
| **PH0 / PH1** | HSE 8MHz 晶振 |

---

## 二、逐步操作

### 步骤 1：删 TIM4 的两个多余通道（先做通道，再删外设）

1. 左侧 **Pinout & Configuration → Timers → TIM3**；
2. 在 **Mode** 区把 `Channel2` 改成 **Disable**、`Channel4` 改成 **Disable**（`Channel1` 保持 `PWM Generation CH1`）；
   - 或在引脚图上**左键点击 PA7 / PB1** → 选择 **Reset_State**；
3. 同理进 **TIM4**，把 `Channel1 / Channel2 / Channel3` 全部改成 **Disable**；
   - 或点击 **PB6 / PB7 / PB8** → **Reset_State**。

> 删除通道后，引脚图上 PA7/PB1/PB6/PB7/PB8 应该变成绿色/灰色的空闲引脚。

### 步骤 2：删 TIM4 外设本身

1. **Timers → TIM4** → 把顶部 **Activated** 的勾**取消**（或 Mode 区全设 Disable）；
2. 回到引脚图确认没有 TIM4 相关的引脚还亮着。

> 若 TIM4 的 Activated 勾选取消不了（还有通道在用），说明步骤 1 没删干净。

### 步骤 3：删 5 条 DMA 流

1. **System Core → DMA → DMA Settings**；
2. 只保留 **`TIM3_CH1/TRIG`** 这一行；
3. 逐条删除：**选中 `TIM3_CH2` 行 → 按 `Delete` 键**（或右键 → `Remove`；不同 CubeMX 版本按钮位置略有差异，也可能在行尾有删除图标）；
4. 依次删掉：

| 要删的 Request | 对应流 | 对应外设 |
|---|---|---|
| `TIM3_CH2` | DMA1_Stream5 | 已删的 PA7 通道 |
| `TIM3_CH4/UP` | DMA1_Stream2 | 已删的 PB1 通道 |
| `TIM4_CH1` | DMA1_Stream0 | 已删的 TIM4 |
| `TIM4_CH2` | DMA1_Stream3 | 已删的 TIM4 |
| `TIM4_CH3` | DMA1_Stream7 | 已删的 TIM4 |

> 删通道后这 5 条有时会**自动消失**（CubeMX 会清理孤立请求）——若已经没了就不用管，
> 只要最后 DMA Settings 里只剩 `TIM3_CH1/TRIG` 即可。

### 步骤 4：删 NVIC 里的冗余中断

**System Core → NVIC**，在 **Enabled** 列取消勾选下面 11 项：

| 要取消的中断 | 原因 |
|---|---|
| `DMA1_Stream0_IRQn` | 对应 TIM4_CH1（已删） |
| `DMA1_Stream2_IRQn` | 对应 TIM3_CH4（已删） |
| `DMA1_Stream3_IRQn` | 对应 TIM4_CH2（已删） |
| `DMA1_Stream5_IRQn` | 对应 TIM3_CH2（已删） |
| `DMA1_Stream7_IRQn` | 对应 TIM4_CH3（已删） |
| `EXTI1_IRQn` / `EXTI2_IRQn` / `EXTI3_IRQn` / `EXTI4_IRQn` | 主机不做击打检测 |
| `EXTI9_5_IRQn` / `EXTI15_10_IRQn` | 同上（KEY5~10） |

**取消勾选后必须保留这 5 个**（确认它们的 Enabled 是勾上的）：

| 中断 | 优先级 | 说明 |
|---|---|---|
| `CAN1_RX0_IRQn` | 1 / 0 | 从机帧 |
| `CAN2_RX0_IRQn` | 1 / 0 | 电机反馈 |
| `TIM6_DAC_IRQn` | 2 / 0 | 1ms 调度 |
| `DMA1_Stream4_IRQn` | 5 / 0 | WS2812 帧完成（**只留这一条 DMA 中断**） |
| `SysTick_IRQn` | 15 / 0 | HAL 时基 |

> 优先级分组保持 **4 bits for pre-emption**。

### 步骤 5：删 KEY1~10 的引脚配置

1. **System Core → GPIO**：把 `KEY1`~`KEY10` 对应的行删掉（选中 → 右键 `Remove`/`Delete`）；
2. 或者直接在引脚图上**左键点击 PA1~PA5、PC6~PC10** → 逐一点选 **Reset_State**；
3. 删完后这些引脚应变为未配置（灰色）。硬件上它们有外部 10kΩ 上拉 + RC 消抖，悬空也不会有干扰。

### 步骤 6：删 PB5（SW4）

**PB5 未装配实物开关、代码也不读**：

- 引脚图点击 **PB5** → **Reset_State**；
- 或 **System Core → GPIO** 删掉 `SW_DEBUG` 那一行。

> 以后新板加了 SW4 开关，回到 CubeMX 点一下 PB5 → `GPIO_Input` 即可恢复。

---

## 三、生成后的校验清单（**逐条核对**）

点 **GENERATE CODE** 后，用下面 5 条快速验收：

### 3.1 `.ioc` 内容核对

```bash
# ① 外设列表: 应该没有 TIM4
grep -E "^Mcu\.IP[0-9]" 2027_Embedded_Buff_Master.ioc
#   期望: CAN1 CAN2 DMA NVIC RCC RNG SYS TIM3 TIM6   (9 项, 无 TIM4)

# ② DMA 请求: 应该只剩 1 条
grep -E "^Dma\.Request[0-9]" 2027_Embedded_Buff_Master.ioc
#   期望: Dma.Request0=TIM3_CH1/TRIG        (且 Dma.RequestsNb=1)

# ③ 关键: DMA 方向仍然正确
grep -E "^Dma\..*Direction" 2027_Embedded_Buff_Master.ioc
#   期望: ...=DMA_MEMORY_TO_PERIPH   (只剩 1 行)
```

### 3.2 生成代码核对

```bash
# ④ GPIO 复用只配 PA6
grep -A 6 "TIM3 GPIO Configuration" Core/Src/tim.c
#   期望: 只出现 "PA6 ------> TIM3_CH1"，无 PA7 / PB1

# ⑤ 中断入口只剩 5 个 + 你的 USER CODE 调度器还在
grep -n "^void .*IRQHandler" Core/Src/stm32f4xx_it.c
#   期望: CAN1_RX0 / CAN2_RX0 / DMA1_Stream4 / TIM6_DAC / SysTick
grep -n "USER CODE BEGIN 1" -A 8 Core/Src/stm32f4xx_it.c
#   期望: HAL_TIM_PeriodElapsedCallback + g_sched_tick_cnt 仍在
```

### 3.3 编译

直接 Build —— 应当 **0 Error 0 Warning**（代码侧已同步为单灯带：
`WS2812_STRIP_NUM=1`，全工程已无 `htim4` 引用）。

### 3.4 上板复测

| 项 | 期望 |
|---|---|
| 上电 | 中心灯带亮（红或蓝，随 SW3） |
| SW3 切换 | 立即变色（下/低=红，上/高=蓝） |
| SW2 = ON | 进白光扫描（只有 1 条→ 该条持续白光）；OFF 恢复常亮 |
| 调试器 Watch `g_sched_tick_cnt` | 持续增长（1ms 中断在跑） |

---

## 四、从机要加回什么 —— ✅ 已完成，明细不再在此维护

主机的裁剪是**角色裁剪，不是硬件删除**：从机要把 10 路击打 EXTI（PA1~PA5、PC6~PC10 + 6 个
EXTI 中断）、TIM3 CH2/CH4、TIM4 CH1/CH2/CH3 以及对应 5 条 DMA 流与 5 个流中断全部加回来。

> 逐步操作与生成后校验：`docs/从机CubeMX操作说明.md`（步骤 1~4），**已执行并校验通过**。
> 长期要记住的只有一条结论：从机的 `WS2812_STRIP_TBL` / `WS2812_STRIP_NUM` 与
> `bsp_ws2812.c` 的缓冲要按实际接线重填（主机版只有 1 条 88 颗，从机是 5~6 条）。

---

## 五、常见坑

| 坑 | 说明 |
|---|---|
| **忘记核对 DMA Direction** | 重新生成后若变成 `DMA_PERIPH_TO_MEMORY` → 灯带立刻不亮 + TIM6 寄存器被失控 DMA 砸坏。**生成后必须 3.1 的第 ③ 条** |
| 误删 PD2/PB3/PB4/PC11/PC12/PB9 | 那是拨码与板卡 ID，删了代码编译不过（`SW_*_Pin`/`BOARD_ID*_Pin` 未定义） |
| 误删 TIM6 | 删了调度就没有了（1ms 节拍是整个架构的心脏） |
| `it.c` 里手写代码被清 | USER CODE 区一般会保留；若发现 `#include "config_board.h"` 等丢失，补回来即可 |
| 删了 KEY 引脚后忘记从机要加回 | 见第四节（从机已加回并校验）；灯带表另见 `config_ws2812.h` 注释 |
| CubeMX 自动清理了孤立 DMA | 正常现象；只要最终只剩 `TIM3_CH1/TRIG` 一条即可 |
