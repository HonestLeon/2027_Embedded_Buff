/**
 * @file config_board.h
 * @brief 板卡角色 / 拨码开关 / 系统节拍 —— 2027 主机固件
 *
 * 规则: config 只放宏与 const 表; 本文件被任何层 include 都安全。
 * 拨码极性已实测(见下), 不再是"待核对"假设。
 */
#ifndef CONFIG_BOARD_H
#define CONFIG_BOARD_H

/* ── 板卡角色 ─────────────────────────────────────────────── */
#define BOARD_ROLE_MASTER        1     /* 1=主机构建; 从机构建改0 (ID1~3自动判定后可废弃) */

/* ── 拨码开关电平极性: 已实测确定(2027 板实物核对) ──
 * 实测结论: 开关 OFF/下 → 引脚低电平, ON/上 → 高电平(内部上拉, OFF 时开关拉到 GND)。
 * 因此 ACTIVE_LEVEL = 1, 且语义与旧固件一致:
 *   高电平 = 转动 / 蓝色 / 大符  (旧代码: test_flag=SW[0]、低=红、BUFF_TYPE=SW[1]) */
#define SW_BUFF_ACTIVE_LEVEL     1     /* SW1 大/小符 PD2: 高=大符, 低=小符 */
#define SW_RUN_ACTIVE_LEVEL      1     /* SW2 停/转  PB3: 高=转(ON), 低=停(OFF) */
#define SW_COLOR_ACTIVE_LEVEL    1     /* SW3 红/蓝  PB4: 高=蓝, 低=红(与旧代码一致) */
/* 注: 原理图上的 SW4(调试模式, PB5) 实物未装配, 旧代码也只定义了 SW[3]
   → 已从 CubeMX 删除该引脚(新板加开关时再加回即可) */

/* ── 拨码采样 ──
 * 老代码每个主循环(≈1ms)读一次; 新架构在 TIM6 1ms 中断里按 SWITCH_POLL_DIV 分频采样。
 * 取 20ms: 机械抖动(<10ms)被滤掉, 同时响应够快(4 次一致 ≈80ms 生效)。
 * ⚠ 早期版本挂在 4Hz 逻辑帧里 + 消抖 4 次 → 要 1 秒才生效, 太慢。 */
#define SWITCH_POLL_DIV          20u   /* 采样周期 = 1ms × 20 = 20ms */
#define SWITCH_DEBOUNCE_TICKS    4u    /* 连续 4 次(≈80ms)一致才生效 */

/* ── 白光测试模式触发方式(仅 P3/P4 调试期) ──
 * 0 = 用编译期宏 DEBUG_TEST_MODE  ← 当前设置(P5 起): SW2 已归还"停/转"语义
 * 1 = 用 SW2 触发(仅电机代码落地前的临时用法)
 * 做灯带通道扫描时: 把 DEBUG_TEST_MODE 置 1, 编译烧录 → 上电即进扫描。 */
#define TEST_TRIGGER_USE_SW2     0
#define DEBUG_TEST_MODE          0     /* =1 时无条件进入白光测试(调试固件专用) */

/* ── 系统节拍 ─────────────────────────────────────────────── */
#define SCHED_TICK_MS            1     /* TIM6 中断周期(1ms), CubeMX: PSC=83/ARR=999 */
#define SCHED_FRAME_DIV          250   /* 逻辑帧 = 1ms×250 = 4Hz (旧代码0.25s原值) */

#endif /* CONFIG_BOARD_H */
