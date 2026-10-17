/**
 * @file config_board.h
 * @brief 板卡角色 / 灯板号 / 拨码开关 / 系统节拍 —— 2027 **从机**(打符灯板)固件
 *
 * 规则: config 只放宏与 const 表; 本文件被任何层 include 都安全。
 * 与主机同名文件的差异仅限: BOARD_ROLE_MASTER=0、新增 SLAVE_PANEL_NUM、SW2 语义、
 * 以及本文件的注释 —— 其余(极性/采样/节拍)与主机保持一致, 改动要两边同步。
 */
#ifndef CONFIG_BOARD_H
#define CONFIG_BOARD_H

/* ── 板卡角色 ─────────────────────────────────────────────── */
#define BOARD_ROLE_MASTER        0     /* 0=从机构建(本工程); 1=主机构建 */

/* ── 本机灯板号: 1~5 —— ⚠ 每块板烧录前改这一处 ─────────────────
 * 沿用 2025 老从机 `#define BUFF_NUM 3` 的"按板编译"方式(不占板卡 ID 引脚)。
 * 主机下行帧的 [3]/[5] 与本值相等时才点亮/熄灭本机(0x03 复位与 0x04 全亮是广播)。
 * ⚠ 5 块板共用一个工程, 极易烧错: **输出固件文件名带上板号**
 *   (Keil: Options→Output→Name for Executable 改成 Buff_Slave_P<n>.hex, n 与本值一致),
 *   联机前逐块核对(《从机重构清单》§六 ⑫)。 */
#define SLAVE_PANEL_NUM          5u

#if (SLAVE_PANEL_NUM < 1u) || (SLAVE_PANEL_NUM > 5u)
#error "SLAVE_PANEL_NUM 必须是 1~5(与 config_buff.h 的 BUFF_PANEL_NUM 一致), 否则本机永不响应点亮帧"
#endif

/* ── 拨码开关电平极性: 已实测确定(2027 板实物核对, 主从同一块 PCB) ──
 * 实测结论: 开关 OFF/下 → 引脚低电平, ON/上 → 高电平(内部上拉, OFF 时开关拉到 GND)。
 * 因此 ACTIVE_LEVEL = 1, 且语义与旧固件一致:
 *   高电平 = 转动 / 蓝色 / 大符  (旧代码: test_flag=SW[0]、低=红、BUFF_TYPE=SW[1]) */
#define SW_BUFF_ACTIVE_LEVEL     1     /* SW1 大/小符 PD2: 高=大符, 低=小符 */
#define SW_RUN_ACTIVE_LEVEL      1     /* SW2 停/转  PB3: 主机用(从机无电机) */
#define SW_COLOR_ACTIVE_LEVEL    1     /* SW3 红/蓝  PB4: 高=蓝, 低=红(与旧代码一致) */

/* ⚠⚠【从机构建】本板**未接拨码开关**(实测): PD2/PB3/PB4 悬空 + 内部上拉 →
 *   三个 SW_* 恒为"高" = 恒为"激活"值。因此从机构建里:
 *   ① 绝不能用本机拨码做逻辑判断(颜色/大小符一律取 CAN 帧的 [4]/[2]);
 *   ② 更不能拿 SW2 当自检触发 → 会一上电就永久扫描(TEST_TRIGGER_USE_SW2 已置 0);
 *   ③ 灯带自检只能用编译期宏 DEBUG_TEST_MODE=1 编调试固件(上电即扫)。 */
/* 注: 原理图上的 SW4(调试模式, PB5) 实物未装配, 旧代码也只定义了 SW[3]
   → 已从 CubeMX 删除该引脚(新板加开关时再加回即可) */

/* ── 拨码采样 ──
 * 老代码每个主循环(≈1ms)读一次; 新架构在 TIM6 1ms 中断里按 SWITCH_POLL_DIV 分频采样。
 * 取 20ms: 机械抖动(<10ms)被滤掉, 同时响应够快(4 次一致 ≈80ms 生效)。
 * ⚠ 早期版本挂在 4Hz 逻辑帧里 + 消抖 4 次 → 要 1 秒才生效, 太慢。 */
#define SWITCH_POLL_DIV          20u   /* 采样周期 = 1ms × 20 = 20ms */
#define SWITCH_DEBOUNCE_TICKS    4u    /* 连续 4 次(≈80ms)一致才生效 */

/* ── 灯带自检(逐条白光)触发方式 ──
 * ⚠ 本板**未接拨码开关** → SW2 读不到, 所以从机只能用编译期宏触发:
 *   0 = 只用 DEBUG_TEST_MODE(编一份调试固件, 上电即逐条扫)   ← 从机的唯一可用方式
 *   1 = 用 SW2 触发(主机专属; 从机若置 1, 悬空上拉的 PB3 恒高 → 一上电就永久扫描)
 * 定位与判读见 docs/《从机灯带自检与排障》(可选工具, 非标定阶段)。 */
#define TEST_TRIGGER_USE_SW2     0
#define DEBUG_TEST_MODE          0     /* =1 时无条件进入灯带自检(调试固件专用) */

/* ── 系统节拍 ─────────────────────────────────────────────── */
#define SCHED_TICK_MS            1     /* TIM6 中断周期(1ms), CubeMX: PSC=83/ARR=999 */
#define SCHED_FRAME_DIV          250   /* 逻辑帧 = 1ms×250 = 4Hz (旧代码0.25s原值) */

#endif /* CONFIG_BOARD_H */
