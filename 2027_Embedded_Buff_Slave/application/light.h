/**
 * @file light.h
 * @brief 从机灯板图案库(S6) —— 四种场景外观, 纯复刻 2025 老从机 `Divice/WS2812.c`
 *
 * 口径(《从机代码重构计划》S6): 本阶段只做老代码画过的图案;
 * 中部灯臂进度 n/5 是老代码从未实现的新增功能, 归 S8 专项。
 */
#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>
#include "buff.h"          /* slv_state_t: 场景 = 从机状态机的四个状态 */

void light_init(void);

/* 状态转换入口调用: 记场景(+环数)并整版清屏 —— 全工程唯一清屏点(差异表 #12) */
void light_set_scene(slv_state_t scene, uint8_t circle);

/* [4] 颜色逐帧生效(老 Decode 对任何 0xAA 帧都刷 color_type, 不看板号 —— 差异表 #11) */
void light_set_color(bool is_blue);

/* 4Hz: 按当前场景重画(待激活的箭头相位每 LIGHT_ARROW_STEP_TICKS 帧步进一次) + 白光自检 */
void light_tick(void);

#endif /* LIGHT_H */
