/**
 * @file bsp_ws2812.h
 * @brief WS2812 六路驱动 —— RGB 逻辑缓冲 / dirty-busy 握手 / PWM+DMA 发送
 *
 * 时序: PWM 周期=1bit(1.25µs@800kHz), DMA 灌 CCR, 周期(ARR)永不改动。
 * ⚠ ch_idx 编码: htim->Channel 的 ACTIVE_CHANNEL_x 是 0x01/0x02/0x04/0x08
 *   (hal_tim.h L324~328, 位掩码式), init 时由 1u<<(channel>>2) 换算。
 */
#ifndef BSP_WS2812_H
#define BSP_WS2812_H

#include <stdint.h>
#include "main.h"

typedef struct
{
    TIM_HandleTypeDef *tim;         /* 绑定的定时器                     */
    uint32_t           channel;     /* PWM 通道 (TIM_CHANNEL_x)         */
    uint16_t           led_num;     /* 灯数, 0=不驱动                   */
    uint16_t          *ccr_buf;     /* DMA 缓冲: led_num*24 + RESET_BITS */
    uint8_t           *rgb;         /* 逻辑帧缓冲: led_num*3 (GRB 字节序)*/
    uint8_t            ch_idx;      /* htim->Channel 匹配用(ACTIVE 编码)*/
    volatile uint8_t   dma_busy;    /* DMA 正在发送                     */
    volatile uint8_t   dirty;       /* 逻辑缓冲有新内容待发             */
} ws2812_strip_t;

void bsp_ws2812_init(void);
void bsp_ws2812_set_rgb(uint8_t strip, uint16_t start, uint16_t count,
                        uint8_t r, uint8_t g, uint8_t b);
void bsp_ws2812_fill(uint8_t strip, uint8_t r, uint8_t g, uint8_t b);
void bsp_ws2812_refresh_all(void);      /* TIM6 1ms 中断里调, 每 tick 最多启动 2 条 */
uint16_t bsp_ws2812_led_num(uint8_t strip);   /* 该灯带灯数, 0=本机未接(供测试遍历用) */

#endif /* BSP_WS2812_H */
