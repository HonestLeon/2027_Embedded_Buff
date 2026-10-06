/**
 * @file config_light.h
 * @brief 灯板图案标定(2025 原值, ⚠️ 勿改) + 从机侧灯效参数 —— 2027 **从机**固件
 *
 * ── 以下为 2025 赛季灯板图案标定, 逐颗数出, ⚠️ 原值勿改(见《重构对照》核对表)──
 *   从机就是这些图案的主人: 老 Buff_Activating / Activatable / Arrowhead 都是从机代码。
 *   段号是"第几颗灯珠", 画在哪条灯带由下面 SLV_STRIP_* 决定 —— 两者都要以 S2 实测为准。
 *   主机同名文件里这些表是"备查副本"(主机不引用), 改动同样**只许搬位置、不许改值**。
 */
#ifndef CONFIG_LIGHT_H
#define CONFIG_LIGHT_H

/* 环数→LED段(灯板靶面, 1~10环 {起始,长度}) —— 老Buff_Activating原值 */
#define LIGHT_RING_MAP { \
    {258,4},{250,8},{237,13},{221,16},{196,25},   /* 1~5环 */ \
    {166,30},{133,33},{94,39},{49,45},{0,49},      /* 6~10环 */ \
}

/* 待激活图案段表 —— 老Buff_Activatable原值(从机灯板用, 共19段) */
#define LIGHT_ARMED_SEGS { {0,1},{12,2},{24,2},{36,2},{48,46},{94,1},{103,2}, \
    {113,2},{122,3},{132,2},{141,2},{149,2},{157,2},{165,2},{173,2},{181,1}, \
    {188,2},{196,25},{250,8} }

/* 箭头模板行 {起始,宽度} —— 老Buff_Arrowhead原值 */
#define LIGHT_ARROWHEAD_ROWS { {3,2},{10,4},{17,2},{21,2},{24,2},{30,2} }

/* ── 【待清理】以下两项是当前仍在编译的**主机版** light.c 使用的(从机 S6 会换掉它) ──
 * 位掩码 = WS2812_STRIP_TBL 的行号; 保留是为了 S1~S5 期间从机能正常编译/点亮灯带调试 */
#define LIGHT_LOCAL_STRIP_MASK  (1u<<0)

/* 白光测试: 每条持续时间(4Hz tick数), 8×250ms=2s —— S2 通道扫描要用 */
#define LIGHT_TEST_TICKS        8u

/* ── 从机侧灯效参数(S1 新增) ────────────────────────────────
 * 索引 = config_ws2812.h 的 WS2812_STRIP_TBL 行号；对应关系取自 2025 老从机代码，
 * ⚠ **哪条是靶面/灯臂/进度条要等 S2 通道扫描 + 机械布局(H5)确认后再定案**。 */
#define SLV_STRIP_TARGET      1u   /* 靶面(老 LED_ROAD_2 "中", 262) —— 环数 + 待激活图案 */
#define SLV_STRIP_ARM         2u   /* 灯臂(老 LED_ROAD_4, 320) —— 箭头流动 + 进度条 */
#define SLV_STRIP_ARM_SIDE    3u   /* 侧灯臂(老 LED_ROAD_5, 120) —— 进度条 */
#define SLV_STRIP_UP          0u   /* 上(老 LED_ROAD_3, 87) */
#define SLV_STRIP_DOWN        4u   /* 下(老 LED_ROAD_1, 92) */

/* 亮度(老原值): 环数/待激活用满亮度, 进度条与"全激活"是很暗的 10 */
#define SLV_LEVEL_FULL        255u /* 环数显示 / 待激活图案 */
#define SLV_LEVEL_PROGRESS     10u /* 进度条灯带整条亮度(老值) */
#define SLV_LEVEL_ALL_ON       10u /* 全激活亮度(老值, ⚠ 外观待与规则图 5-23 核对) */
#define SLV_LEVEL_SLEEP         0u /* 休眠 = 全灭 */

/* 老"全激活"(state 4)只点亮 上/下 两条 —— S6.5 与规则图核对后再定要点几条 */
#define SLV_ALL_ON_STRIP_MASK  ((1u<<SLV_STRIP_UP)|(1u<<SLV_STRIP_DOWN))

/* 进度条分 5 段(规则 5.5.2.1: 灯臂中部指示激活进度 n/5)，每段灯数运行时按灯带总长算 */
#define SLV_PROGRESS_STEPS     5u

#endif /* CONFIG_LIGHT_H */
