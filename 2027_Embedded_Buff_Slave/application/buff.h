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

/* ── 从机回报(从机 → 主机, 老协议: [1]==0x03 被击中 / [2]==0x01 打错复位) ── */
typedef enum
{
    SLAVE_RPT_HIT,      /* 被点亮/被击中, panel = 灯板号 1~5 */
    SLAVE_RPT_RESET,    /* 打错, 从机已自行复位 */
} slave_rpt_kind_t;

typedef struct
{
    slave_rpt_kind_t kind;
    uint8_t          panel;
} slave_report_t;

bool buff_slave_report_parse(const uint8_t in[8], slave_report_t *r);

/* ── 随机序列(老算法原样, 去 srand 化) ──
 * 用前请在上电时调用一次 srand(bsp_rng_get_seed()) (main.c 已做) */
void buff_random_small(uint8_t arr[], uint8_t length, uint8_t min, uint8_t max);
void buff_random_big(uint8_t arr1[], uint8_t arr2[], uint8_t length, uint8_t min, uint8_t max);

/* ── 调试观察量(长期保留) ── */
extern volatile uint32_t g_dbg_can1_rx_frames;   /* 收到的从机帧累计数 */
extern volatile uint8_t  g_dbg_buff_state;       /* 状态机当前状态 */
extern volatile uint8_t  g_dbg_buff_round;       /* 当前轮次 */

/* ── 大小符状态机(与《代码架构设计》3.3 转换表一致) ── */
typedef enum
{
    BB_RESET = 0,       /* 复位: 重取随机序列 + 发全复位帧(从机回休眠) */
    BB_IDLE,            /* 待激活: 尚未点亮第一组 */
    BB_LIT,             /* 已点亮: 等待被击中(第一组不设超时, 之后 2.5s) */
    BB_HIT_WINDOW,      /* 大符首击后的 1s 窗口 */
    BB_COOLDOWN,        /* 熄灭旧对 → 点亮新对的间隔 */
    BB_ALL_ON,          /* 全亮展示, 2.5s 后复位 */
} bb_state_t;

/* ── 状态机 ── */
void buff_init(void);
void buff_step(void);           /* 4Hz: 排空 bsp_can1_pop + 拨码变化 → 转换/发帧 */
void buff_tick(float dt);       /* 1ms: 2.5s超时 / 1s窗口 计时 */

#endif /* BUFF_H */
