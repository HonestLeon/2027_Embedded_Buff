/**
 * @file buff.c
 * @brief 从机灯板业务 —— CAN 协议 + 状态机 + 随机序列(单文件模块)
 *
 * P4 已实现: 协议 pack/parse(主从共享的唯一真相源) + 发送封装 + 联调钩子;
 * P6 待实现: 大小符状态机(buff_step/buff_tick) 与随机序列(去 srand 化)。
 *
 * 协议(与 2025 老固件逐字节兼容, 主从必须一致):
 *   [0]=0xaa 包头  [1]=命令(0x01熄灭/0x02点亮/0x03复位/0x04全亮)
 *   [2]=0x01大符/0x00小符  [3]=灯板序号1~5  [4]=0红/1蓝
 *   [5]=大符第二块灯板序号  [6]=大符第几次激活双灯板  [7]=保留0
 */
#include "buff.h"
#include "config_buff.h"
#include "config_can.h"
#include "bsp_can.h"
#include "bsp_switch.h"

#define BF_HEAD                      0xAAu   /* 包头 */
#define BF_LEN                       8u
#define BF_IDX_HEAD                  0u
#define BF_IDX_CMD                   1u
#define BF_IDX_TYPE                  2u
#define BF_IDX_PANEL_A               3u
#define BF_IDX_COLOR                 4u
#define BF_IDX_PANEL_B               5u
#define BF_IDX_ROUND                 6u
#define BF_IDX_RESERVED              7u

/* ── P4 联调观察量(临时, BUFF_P4_CAN_TEST 关闭后可删) ── */
volatile uint32_t g_dbg_can1_rx_frames = 0u;   /* 收到的从机帧累计数 */

/* ── 协议: 打包 ───────────────────────────────────────────── */
uint8_t buff_frame_pack(uint8_t out[8], const buff_frame_t *f)
{
    out[BF_IDX_HEAD]     = BF_HEAD;
    out[BF_IDX_CMD]      = (uint8_t)f->cmd;
    out[BF_IDX_TYPE]     = f->is_big ? 0x01u : 0x00u;
    out[BF_IDX_PANEL_A]  = f->panel_a;
    out[BF_IDX_COLOR]    = f->is_blue ? 1u : 0u;
    out[BF_IDX_PANEL_B]  = f->panel_b;
    out[BF_IDX_ROUND]    = f->round;
    out[BF_IDX_RESERVED] = 0u;
    return BF_LEN;
}

/* ── 协议: 解包(校验包头与字段范围, 非法返回 false) ───────── */
bool buff_frame_parse(const uint8_t in[8], buff_frame_t *f)
{
    if (in[BF_IDX_HEAD] != BF_HEAD)
    {
        return false;
    }
    if (in[BF_IDX_CMD] < (uint8_t)BF_EXTINGUISH ||
        in[BF_IDX_CMD] > (uint8_t)BF_ALL_ON)
    {
        return false;
    }
    if (in[BF_IDX_PANEL_A] > BUFF_PANEL_NUM || in[BF_IDX_PANEL_B] > BUFF_PANEL_NUM)
    {
        return false;
    }

    f->cmd     = (bf_cmd_t)in[BF_IDX_CMD];
    f->is_big  = (in[BF_IDX_TYPE] == 0x01u) ? 1u : 0u;
    f->panel_a = in[BF_IDX_PANEL_A];
    f->panel_b = in[BF_IDX_PANEL_B];
    f->is_blue = (in[BF_IDX_COLOR] != 0u) ? 1u : 0u;
    f->round   = in[BF_IDX_ROUND];
    return true;
}

/* ── 发送封装: 打包 + CAN1 发送 ───────────────────────────── */
bool buff_send_frame(const buff_frame_t *f)
{
    uint8_t raw[BF_LEN];

    (void)buff_frame_pack(raw, f);
    return bsp_can1_send(CAN_ID_MASTER_TO_SLAVE, raw);
}

/* ── 状态机(P6 实现) ──────────────────────────────────────── */

void buff_init(void)
{
    /* TODO P6.1: 取随机序列(算法保留、去 srand 化, 种子由上电 bsp_rng_get_seed 注入), 清计数 */
}

void buff_step(void)
{
#if BUFF_P4_CAN_TEST
    /* ── P4 临时联调: 每 4Hz 发一帧 0x161(全复位) + 排空并统计收到的从机帧 ──
       验证点: ①CAN 盒能在 CAN1 上看到 4Hz 的 0x161 帧
               ②CAN 盒发 0x160 帧后, g_dbg_can1_rx_frames 递增
       P6 实现状态机后把 BUFF_P4_CAN_TEST 置 0 */
    buff_frame_t tx;
    uint32_t rx_id;
    uint8_t  rx_data[8];

    tx.cmd     = BF_RESET;
    tx.is_big  = 0u;
    tx.panel_a = 0u;
    tx.panel_b = 0u;
    tx.is_blue = (bsp_switch_get().sw_color != 0u) ? 1u : 0u;
    tx.round   = 0u;
    (void)buff_send_frame(&tx);

    while (bsp_can1_pop(&rx_id, rx_data))
    {
        g_dbg_can1_rx_frames++;
        /* TODO P6.3: 此处改成 buff_frame_parse → 事件分发 → 状态转换 */
    }
#else
    /* TODO P6.3: 排空 bsp_can1_pop(灯板回报/复位) + 拨码变化 → 状态转换/发帧 */
#endif
}

void buff_tick(float dt)
{
    (void)dt;
    /* TODO P6.2: 2.5s超时 / 1s窗口 计时 */
}
