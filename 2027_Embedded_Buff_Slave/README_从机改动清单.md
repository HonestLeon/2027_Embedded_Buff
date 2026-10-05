# 从机工程起手模板 —— 改动清单

> **这是什么**：`2027_Embedded_Buff_Master/` 在 commit `e73c3b7`（P3 完成）时的快照，
> 直接当作**从机（打符灯板）工程**的起手基础。
>
> **不含 `Drivers/`**（HAL/CMSIS，约 57MB）：第一次在 CubeMX 里 **Generate Code 会自动补齐**。
>
> **使用方式**：本目录建议改名为 `2027_Embedded_Buff_Slave/`，
> 在 CubeMX 里把 `Project Name` 改成 `2027_Embedded_Buff_Slave` 后重新生成即可（会同步重命名 .ioc/.uvprojx）。

---

## 一、主从差异一句话总结

| | 主机（中心板） | 从机（打符灯板） |
|---|---|---|
| 灯带 | **1 条**：中心（TIM3_CH1/PA6/88 颗） | **5 条**（口径待复核：92 / 262 / 87 / 320 / 120） |
| 击打检测 | 不做 | **核心职责**：KEY1~10 → 判环数 → CAN 上报 |
| 电机 | 有（双环 PID + 正弦前馈，CAN2） | 无 |
| CAN | CAN1 发指令给从机、收回报；CAN2 控电机 | CAN1 收指令（0x161）、发回报（0x160） |
| 灯效 | 开机红蓝、本地动画 | 待激活呼吸/箭头、环数显示、全亮、进度条 |
| 逻辑帧 | 4Hz 大小符状态机 | 状态机（休眠/待激活/已激活/全激活）+ 击打事件 |

---

## 二、CubeMX 要加回来的（**主机当初裁掉了，从机必须恢复**）

> 裁剪的来龙去脉见上级目录《CubeMX裁剪说明.md》第四节。

### 2.1 击打检测输入（10 个引脚 + 6 个中断）

| 项 | 内容 |
|---|---|
| 引脚 | **PA1~PA5、PC6~PC10** → 全部配 `GPIO_EXTIx`（**双边沿**，实测极性后再收敛） |
| Pull | **No pull-up and no pull-down**（硬件已有 1k 串阻 + 10k 上拉 + 10µF 消抖） |
| User Label | **KEY1 ~ KEY10**（与旧从机 `main.h` 的 RDET1~10 引脚一致） |
| NVIC | `EXTI1/2/3/4_IRQn`、`EXTI9_5_IRQn`（含 PA5+PC6~9）、`EXTI15_10_IRQn`（PC10），抢占优先级 **3** |

> 引脚↔环数标定（老代码口径，勿改）：KEY1→10环 … KEY5→6环，KEY6~10 依次对应 5~1 环
> （见《重构对照》第 6 节 `hit_map`）。

### 2.2 其余灯带（TIM4 全通道 + TIM3 的 CH2/CH4）

| 加回项 | 通道 / 引脚 | 老固件口径灯数 |
|---|---|---|
| TIM3 CH2 | PA7 | 262（"中"，靶面/环数显示） |
| TIM3 CH4 | PB1 | 320（"灯臂"） |
| TIM4 外设 + CH1 | PB6 | 120（"侧灯臂"） |
| TIM4 CH2 | PB7 | 92（"下"） |
| TIM4 CH3 | PB8 | （老从机未驱动，待核实是否接灯带） |

- PWM 参数与其他通道**完全一致**：`PSC=2`、`ARR=34`、PWM1、Pulse=0、极性 High；
- 每条通道各配 1 条 DMA 流：

| 通道 | DMA 流 |
|---|---|
| TIM3_CH2 | DMA1_Stream5 |
| TIM3_CH4 | DMA1_Stream2 |
| TIM4_CH1 | DMA1_Stream0 |
| TIM4_CH2 | DMA1_Stream3 |
| TIM4_CH3 | DMA1_Stream7 |

- **每条流的 Direction = `Memory To Peripheral`**（⚠⚠ 见第五节，配反会灯不亮 + 砸坏别的外设寄存器）
- `Peripheral Increase = 不勾`、`Memory Increase = 勾`、双方 **Half Word**、Mode = **Normal**、Priority = Low；
- 每加入 1 条流，同时使能对应的 `DMA1_Streamx_IRQn`（抢占优先级 **5**）。

### 2.3 保留不动的

TIM6（1ms 调度）、CAN1、CAN2、RNG、拨码 PD2/PB3/PB4、板卡 ID PC11/PC12/PB9、SWD、HSE。
（PB5 实物未装配，若要加调试开关可再配为 GPIO 输入。）

---

## 三、代码要改的（逐文件）

### 3.1 `config/config_board.h`

```c
#define BOARD_ROLE_MASTER   0      /* 改成 0 = 从机构建 */
```
- 拨码极性沿用实测结论（`*_ACTIVE_LEVEL = 1`：OFF/下=低、ON/上=高）；
- `TEST_TRIGGER_USE_SW2`：从机也可以先留着（SW2 在从机上若空闲，同样可做灯带扫描）；
  将来电机/本地逻辑占用 SW2 时改成 0。

### 3.2 `config/config_ws2812.h`（**核心**）

按从机实际接线重填灯带表，并同步改 `bsp/bsp_ws2812.c` 的缓冲：

```c
#define WS2812_STRIP_TBL { \
    { &htim3, TIM_CHANNEL_1,  <实测灯数> },  /* 上灯带   */ \
    { &htim3, TIM_CHANNEL_2,  <实测灯数> },  /* 中灯带   */ \
    { &htim3, TIM_CHANNEL_4,  <实测灯数> },  /* 灯臂     */ \
    { &htim4, TIM_CHANNEL_1,  <实测灯数> },  /* 侧灯臂   */ \
    { &htim4, TIM_CHANNEL_2,  <实测灯数> },  /* 下灯带   */ \
    { &htim4, TIM_CHANNEL_3,  <实测灯数> },  /* 待核实   */ \
}
#define WS2812_STRIP_NUM   (条数)
```

**老从机固件口径（参考，需实测复核）**：`92 / 262 / 87 / 320 / 120`
（`WS2812.h`：1=下 92、2=中 262、3=上 87、4=灯臂 320、5=侧灯臂 120）。

`bsp_ws2812.c` 里对应补回缓冲：

```c
static uint16_t s_ccr0[WS_CCR_LEN(92u)];   /* 每条一个, 灯数填实测值 */
static uint8_t  s_rgb0[WS_RGB_LEN(92u)];
...
static uint16_t *const s_ccr_tbl[WS2812_STRIP_NUM] = { s_ccr0, s_ccr1, ... };
```

### 3.3 `config/config_light.h`

- `LIGHT_LOCAL_STRIP_MASK`：从机的**全部实接灯带**都给本地灯色（改成对应位掩码）；
- `LIGHT_RING_MAP` / `LIGHT_ARMED_SEGS` / `LIGHT_ARROWHEAD_ROWS`：**2025 标定原值，逐颗数出来的，勿改**
  （环数表是针对 262 颗那条"中灯带"的；待激活 19 段、箭头模板同理）。

### 3.3.1 ⚠ 从机侧必须实现的规则要求（2026 规则 5.5.2.1）

主机只负责"点亮哪一组/哪几块"和"复位"，**视觉效果与"打错"判定都在从机**。以下必须由从机实现：

| 规则原文 | 从机要做的事 |
|---|---|
| "被点亮的装甲模块亮起**特殊灯效**" | 待激活图案（老 `Buff_Activatable`） |
| "该装甲模块对应的**灯臂中轴有箭头状流动灯效**" | 箭头流动（老 `Buff_Arrowhead_Flowing`） |
| "该灯臂会被**完全点亮**并亮起相应灯效" | 被击中后显示环数（老 `Buff_Activating`） |
| "**大**能量机关的灯臂**中部灯效**将指示激活进度…亮起约 **1/5、2/5**…" | **按主机发来的组号（帧内 `[6]` 字节）实现 k/5 进度条**（老代码只是把灯带整条调暗，不是 k/5，需要重做） |
| "若5 组灯臂全部激活…已激活状态" | 全亮图案（收到 `[1]=0x04`） |
| **激活失败**: "击中**非随机点亮的**装甲模块" | 命中未点亮的装甲模块时，**主动回传复位帧**（`[0]=0xAA, [2]=0x01`）——主机据此复位并重新随机 |
| 未激活状态 | 休眠/未激活图案（收到 `[1]=0x01` 或 `0x03`） |

> 注意：老从机的"打错复位"是靠 `once` 标志(0/1/2 三态)实现的，重构时建议改成显式状态
> （armed/fired/locked），并把"当前是否被点亮"作为能否击中的唯一判据。

### 3.4 `application/` 新增与改动

| 文件 | 动作 |
|---|---|
| **`hit_detect.c/.h`** | **新增**（主机构建里没有）：EXTI 回调只记时间戳+置位；逻辑帧查表判环数（KEY→环数），维护 `armed/fired/locked` 显式状态；参考《重构对照》第 6 节 |
| `buff.c` | **复用协议段**（`buff_frame_pack/parse` 直接可用）；状态机要换成**从机侧**的"休眠/待激活/已激活/全激活"（老 `slave/main.c` 的 state 机），并**主动上报**击打（发 0x160） |
| `light.c` | 图案换成老从机的：`Buff_Activatable`（待激活）、`Buff_Activating`（环数）、`Buff_Arrowhead_Flowing`（箭头流动）、进度条、全亮 |
| `motor.c/.h` | **不需要**（从机无电机）→ 从 Keil Group 移除或留空不调用 |
| `main.c` | USER CODE 2 的初始化里去掉 `motor_init()`；`it.c` 调度里去掉 `motor_update()`，改为调用击打任务 |
| `it.c` | 1ms 节拍：`hit_detect` 时间戳/超时 + `bsp_ws2812_refresh_all()`；4Hz 帧：拨码采样 + CAN 出队 + 从机状态机 + 灯效 |

### 3.5 Keil 工程

`Manage Project Items`：`bsp` Group 不变；`application` Group 加入 `hit_detect.c`；
若移除 `motor.c` 则同时从 Group 删除。

---

## 四、主从共享的协议（唯一真相源）

**`buff.c` 里的协议段（`bf_cmd_t` / `buff_frame_t` / `buff_frame_pack` / `buff_frame_parse`）
是主从共用的**，任何改动必须两边同步：

| 字节 | 语义 |
|---|---|
| [0] | 0xaa 包头 |
| [1] | 命令：0x01 熄灭 / 0x02 点亮 / 0x03 复位 / 0x04 全亮 |
| [2] | 0x01 大符 / 0x00 小符 |
| [3] | 灯板序号 1~5 |
| [4] | 0 红 / 1 蓝 |
| [5] | 大符第二块灯板序号 |
| [6] | 大符第几次激活双灯板 |

**同步规则**：改协议时在两份 `buff.c` 上做 `diff` 确认；或把协议段抽成独立文件
（`buff_protocol.c/.h`）在两工程各放一份，保持逐字节一致。CAN ID：
主机发 `0x161`、从机发 `0x160`（`config_can.h`）。

---

## 五、⚠️ 两个必须检查的坑

### 5.1 DMA 方向必须是 `Memory To Peripheral`

从机要加 **5 条新 DMA 流**，每条都要按这个方向配。配成 `Peripheral To Memory` 的后果
（已实际踩过）：`HAL_DMA_Start_IT` 会把源地址当成"外设地址"、目标地址当成"内存地址"，
于是**源永远读 buffer[0]、目标地址每 +2 递增**，一路从 `CCRx` 写到 `BDTR/DCR/DMAR/OR`
并冲出 TIM 寄存器块，砸坏 TIM4/TIM5/**TIM6（1ms 调度）**/TIM7 → 灯不亮 + 调度失效。
判据：生成代码里必须是 `Init.Direction = DMA_MEMORY_TO_PERIPH;`。

### 5.2 从机灯带通道 ↔ 环数/图案标定要对上

老从机 `WS2812.c` 里的 `Buff_Activating`（环数）画在 `LED_ROAD_2`（262 颗那条）上，
`load_num_1~5` 是"未激活时跳过多少颗"的标定值 —— 这些**数字都是逐颗数出来的**，
在新代码里只能搬位置、不能改数值。

---

## 六、参考资料（仓库 `docs/` 目录）

| 文档 | 用途 |
|---|---|
| 《代码架构设计.md》 | 分层规则、模块职责、调度设计（**先读这个**） |
| 《重构对照-旧代码与新设计.md》 | 老固件逐行分析 + 标定数据继承核对表 + `hit_map`/环数表来源 |
| 《主机CubeMX配置清单.md》 | CubeMX 配置依据（从机参考其中的 WS2812/CAN/EXTI 部分） |
| 《CubeMX裁剪说明.md》 | 第四节 = 从机要加回的完整清单 |
| 《白光测试指南.md》 | 通道扫描定位法（从机有 5 条灯带，**上电必做**，用来核对灯数与通道） |
| 《P3调试指南.md》 | 灯带不亮时的排查流程（DMA 方向那节就是血的教训） |
