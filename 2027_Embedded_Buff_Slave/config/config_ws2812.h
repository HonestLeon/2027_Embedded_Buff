/**
 * @file config_ws2812.h
 * @brief WS2812 驱动时序 + 灯带表(bsp_ws2812.c 使用)
 *
 * ⚠ 引用本文件中 WS2812_STRIP_TBL 前, 必须先 include "tim.h"
 *   (表内使用 &htim3 句柄 —— config 层对 HAL 句柄声明的唯一放宽项)。
 *
 * ✅ 2026 实测: 主机实接灯带只有"中心"一条 —— TIM3_CH1/PA6/88颗(详见下方灯带表)。
 */
#ifndef CONFIG_WS2812_H
#define CONFIG_WS2812_H

/* ── 时序(2025 老板实测值, 逻辑分析仪可复核) ──────────────────
 * 定时器计数频率 = 84MHz(APB1 Timer)/(PSC+1) = 84M/3 = 28MHz → tick≈35.7ns
 * bit 率 = 28MHz/(ARR+1) = 28M/35 = 800kHz (bit 周期 1.25µs)           */
#define WS2812_T0H_CCR           10u   /* "0"码高电平 10 tick = 357ns (规格0.4µs±150ns) */
#define WS2812_T1H_CCR           25u   /* "1"码高电平 25 tick = 893ns (规格0.8µs±150ns) */
#define WS2812_RESET_BITS        90u   /* 复位段 bit 数 = 112µs (规格 >50µs, 老代码50bit偏紧) */

/* ── 灯带表: {TIM句柄, 通道, 灯数} ───────────────────────────
 * ✅ 2026 实测定位完毕(通道扫描法): 主机板实接灯带**只有 1 条 —— "中心"**,
 *    挂在 **TIM3_CH1 = PA6 = 88 颗**(老固件 LED_ROAD_3);
 *    ⚠ 板上丝印把 PA6 标为 "DIN6(无标注)", 而把 "DIN2 中心圆环" 标在 TIM4_CH1(PB6, 空置口) 上
 *      —— 丝印与实物接法错位, **以实测为准**。
 *    CubeMX 中已裁掉 TIM4 与 TIM3_CH2/CH4 及其 DMA 流(不接灯带, 无需驱动)。
 *
 * 建从机工程时: 需回 CubeMX 加回 TIM4 与 TIM3_CH2/CH4 + 各自 DMA 流,
 * 并按从机接线重填本表(从机口径: 92/262/87/320/120, TIM4_CH1=侧灯臂)。 */
#define WS2812_STRIP_TBL { \
    { &htim3, TIM_CHANNEL_1,   88u },  /* ★中心(PA6) —— 主机唯一实接灯带 */ \
}
#define WS2812_STRIP_NUM         1u

#endif /* CONFIG_WS2812_H */
