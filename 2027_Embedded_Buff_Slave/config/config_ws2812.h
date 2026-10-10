/**
 * @file config_ws2812.h
 * @brief WS2812 驱动时序 + 灯带表(bsp_ws2812.c 使用)
 *
 * ⚠ 引用本文件中 WS2812_STRIP_TBL 前, 必须先 include "tim.h"
 *   (表内使用 &htim3/&htim4 句柄 —— config 层对 HAL 句柄声明的唯一放宽项)。
 *
 * 本文件是**从机版**: 6 个通道全在(TIM3 CH1/CH2/CH4 + TIM4 CH1/CH2/CH3)，其中 5 条接灯带；
 * 灯数直接继承 2025 老从机口径(硬件未变，无需标定)——详见下方灯带表注释。
 * 主机版只有 1 条(TIM3_CH1/PA6/88 颗, 实测) —— 两份灯带表本来就不同, 不要互相覆盖。
 */
#ifndef CONFIG_WS2812_H
#define CONFIG_WS2812_H

/* ── 时序(2025 老板实测值, 逻辑分析仪可复核) ──────────────────
 * 定时器计数频率 = 84MHz(APB1 Timer)/(PSC+1) = 84M/3 = 28MHz → tick≈35.7ns
 * bit 率 = 28MHz/(ARR+1) = 28M/35 = 800kHz (bit 周期 1.25µs)           */
#define WS2812_T0H_CCR           10u   /* "0"码高电平 10 tick = 357ns (规格0.4µs±150ns) */
#define WS2812_T1H_CCR           25u   /* "1"码高电平 25 tick = 893ns (规格0.8µs±150ns) */
#define WS2812_RESET_BITS        90u   /* 复位段 bit 数 = 112µs (规格 >50µs, 老代码50bit偏紧) */

/* ── 灯带表: {TIM句柄, 通道, 灯数} —— 灯数填 0 = 本机该通道不接灯带 ──────
 * 【口径来源】本板硬件与 2025 完全相同 ⇒ 老从机 `WS2812.h` 的灯数与"通道↔功能"绑定
 *   **就是规格，直接继承，无需实测标定**（2026-10 确认）。老代码能正常工作本身就是验证。
 *   ⚠ 数值取自老**从机**工程(87/262/320/120/92)；老**主机**工程的同名宏是 `_3=88/_4=256`，
 *     不要拿主机那份来"纠正"。索引 5(PB8) 老从机从不驱动 → 按老行为填 0(不分配/不发送)。
 *   仅当某条不亮/改过接线时，才用 `DEBUG_TEST_MODE=1` 逐条点亮定位
 *   （从机未接拨码，不能用 SW2 触发）—— 见 docs/《从机灯带自检与排障》。
 *
 * 灯数只在下面 WS_LED_NUM_* 一处定义: 灯带表与 bsp_ws2812.c 的 DMA 缓冲都由它推导,
 * 不会出现"改了表忘了改缓冲"的越界。 */
#define WS_LED_NUM_UP            87u  /* 索引0 TIM3_CH1/PA6  老 LED_ROAD_3 "上"        */
#define WS_LED_NUM_TARGET       262u  /* 索引1 TIM3_CH2/PA7  老 LED_ROAD_2 "中"(靶面)  */
#define WS_LED_NUM_ARM          320u  /* 索引2 TIM3_CH4/PB1  老 LED_ROAD_4 "灯臂"      */
#define WS_LED_NUM_ARM_SIDE     120u  /* 索引3 TIM4_CH1/PB6  老 LED_ROAD_5 "侧灯臂"    */
#define WS_LED_NUM_DOWN          92u  /* 索引4 TIM4_CH2/PB7  老 LED_ROAD_1 "下"        */
#define WS_LED_NUM_SPARE          0u  /* 索引5 TIM4_CH3/PB8 —— 老从机从未驱动该口, 按老行为不驱动。
                                       *   硬件与 2025 相同 ⇒ 老固件行为即规格, 无需实测;
                                       *   填 0 = 不分配缓冲/不发送(省 ~4.4KB)。
                                       *   ⚠ 若将来确认该口接了灯带: 改成实测灯数即可(表与缓冲自动跟随)。 */

#define WS2812_STRIP_TBL { \
    { &htim3, TIM_CHANNEL_1, WS_LED_NUM_UP },       \
    { &htim3, TIM_CHANNEL_2, WS_LED_NUM_TARGET },   \
    { &htim3, TIM_CHANNEL_4, WS_LED_NUM_ARM },      \
    { &htim4, TIM_CHANNEL_1, WS_LED_NUM_ARM_SIDE }, \
    { &htim4, TIM_CHANNEL_2, WS_LED_NUM_DOWN },     \
    { &htim4, TIM_CHANNEL_3, WS_LED_NUM_SPARE },    \
}
#define WS2812_STRIP_NUM         6u

/* 一条灯带都没接时(全填 0)下面这句会编译失败, 提醒检查配置 */
#if (WS_LED_NUM_UP + WS_LED_NUM_TARGET + WS_LED_NUM_ARM + WS_LED_NUM_ARM_SIDE + WS_LED_NUM_DOWN + WS_LED_NUM_SPARE) == 0u
#error "从机 6 条灯带灯数全为 0: 请核对是否要用从机构建(主机工程只需 1 条)"
#endif

#endif /* CONFIG_WS2812_H */
