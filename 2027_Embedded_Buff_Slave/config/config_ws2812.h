/**
 * @file config_ws2812.h
 * @brief WS2812 驱动时序 + 灯带表(bsp_ws2812.c 使用)
 *
 * ⚠ 引用本文件中 WS2812_STRIP_TBL 前, 必须先 include "tim.h"
 *   (表内使用 &htim3/&htim4 句柄 —— config 层对 HAL 句柄声明的唯一放宽项)。
 *
 * 本文件是**从机版**: 6 个通道全在(TIM3 CH1/CH2/CH4 + TIM4 CH1/CH2/CH3), 灯数待 S2 实测。
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
 * ⚠ 下表灯数是 **2025 老从机口径(92/262/87/320/120)**, 还没在 2027 板上实测过!
 *   S2 通道扫描(《白光测试指南》)逐条点亮 + 点数后回填真值, 并把"索引↔物理位置"写进
 *   《从机重构清单》§二 的对照表。主机板曾吃过"丝印与固件口径系统性错位"的亏,
 *   **以实测为准, 不要相信丝印**。
 *
 * 灯数只在下面 WS_LED_NUM_* 一处定义: 灯带表与 bsp_ws2812.c 的 DMA 缓冲都由它推导,
 * 不会出现"改了表忘了改缓冲"的越界。索引 5(PB8) 老从机从未驱动 → 填 0 待核实。 */
#define WS_LED_NUM_UP            87u  /* 索引0 TIM3_CH1/PA6  老 LED_ROAD_3 "上"        */
#define WS_LED_NUM_TARGET       262u  /* 索引1 TIM3_CH2/PA7  老 LED_ROAD_2 "中"(靶面)  */
#define WS_LED_NUM_ARM          320u  /* 索引2 TIM3_CH4/PB1  老 LED_ROAD_4 "灯臂"      */
#define WS_LED_NUM_ARM_SIDE     120u  /* 索引3 TIM4_CH1/PB6  老 LED_ROAD_5 "侧灯臂"    */
#define WS_LED_NUM_DOWN          92u  /* 索引4 TIM4_CH2/PB7  老 LED_ROAD_1 "下"        */
#define WS_LED_NUM_SPARE         90u  /* 索引5 TIM4_CH3/PB8  ⚠ 老从机从未驱动, 是否接线未知。
                                       *   这里**必须先给非 0 值**白光扫描才会去试它
                                       *   (light.c 只遍历 led_num>0 的通道, 填 0 就永远扫不到);
                                       *   S2 实测: 不亮 → 改回 0(不分配/不发送); 亮 → 改成实测灯数。
                                       *   90 是个安全的试探值: 多发的部分只是没接的灯珠, 无害。 */

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
