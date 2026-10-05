/**
 * @file bsp_switch.h
 * @brief 拨码开关(4个)与板卡 ID(3位)读取 —— 消抖后状态查询
 */
#ifndef BSP_SWITCH_H
#define BSP_SWITCH_H

#include <stdint.h>

typedef struct
{
    uint8_t sw_buff;    /* SW1 大/小符 PD2, 生效电平见 config_board.h */
    uint8_t sw_run;     /* SW2 停/转  PB3 (P3/P4 期间兼作白光测试触发) */
    uint8_t sw_color;   /* SW3 红/蓝  PB4 (旧代码: 低=红) */
} switch_state_t;       /* 注: SW4/PB5 实物未装配, 不读取 */

void          bsp_switch_init(void);        /* 记录初始电平(消抖基准)   */
void          bsp_switch_poll(void);        /* 4Hz 逻辑帧调用, 消抖采样 */
switch_state_t bsp_switch_get(void);        /* 取当前消抖后的状态       */
uint8_t       bsp_switch_board_id(void);    /* ID1~3 → 0/1/2 挡位       */

#endif /* BSP_SWITCH_H */
