/**
 * @file buff.h
 * @brief 从机灯板业务 —— CAN协议 + 大小符状态机 + 随机序列(单文件模块)
 *
 * 协议段为 2025 老语义, 主从共用: 从机工程复制本段后以 diff 校验同步。
 */
#ifndef BUFF_H
#define BUFF_H

#include <stdbool.h>
#include <stdint.h>

/* ── 协议 ── */
typedef enum
{
    BF_EXTINGUISH = 0x01,   /* 熄灭旧对     */
    BF_LIGHT      = 0x02,   /* 点亮         */
    BF_RESET      = 0x03,   /* 复位         */
    BF_ALL_ON     = 0x04,   /* 全亮         */
} bf_cmd_t;

typedef struct
{
    bf_cmd_t cmd;           /* 指令                          */
    uint8_t  is_big;        /* 1=大符                        */
    uint8_t  panel_a;       /* 灯板 1~5, 0=无                */
    uint8_t  panel_b;       /* 大符第二块 1~5, 0=无          */
    uint8_t  is_blue;       /* 0=红 1=蓝                     */
    uint8_t  round;         /* 大符第几次激活双灯板 0~5      */
} buff_frame_t;

uint8_t buff_frame_pack(uint8_t out[8], const buff_frame_t *f);   /* 恒返回8 */
bool   buff_frame_parse(const uint8_t in[8], buff_frame_t *f);   /* 校验包头0xaa */
bool   buff_send_frame(const buff_frame_t *f);                   /* 打包 + CAN1(0x161) 发送 */

/* P4 联调观察量(BUFF_P4_CAN_TEST 期间有效): 收到的从机帧累计数 */
extern volatile uint32_t g_dbg_can1_rx_frames;

/* ── 状态机 ── */
void buff_init(void);
void buff_step(void);           /* 4Hz: 排空 bsp_can1_pop + 拨码变化 → 转换/发帧 */
void buff_tick(float dt);       /* 1ms: 2.5s超时 / 1s窗口 计时 */

#endif /* BUFF_H */
