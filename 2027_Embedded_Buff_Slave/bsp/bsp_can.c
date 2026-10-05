/**
 * @file bsp_can.c
 * @brief CAN 双总线收发(P4 完整实现)
 *
 * 总线分工(接线事实): CAN1 = 主从互联(从机灯板), CAN2 = 电机控制。
 * 过滤器: MASK 模式, 32 位, 各总线一套 —— CAN1 收 0x160~0x167, CAN2 收 0x200~0x207。
 *         CAN2 的 FilterBank 必须 >= SlaveStartFilterBank(=14), 否则不生效。
 * 接收  : 中断里只 GetRxMessage + 按 hcan 入对应队列; 业务侧 pop 消费。
 *         两条总线各自独立队列 —— 电机反馈 ~1kHz×4 与从机帧 4Hz 混在一起会互相拖延。
 * 发送  : 查一次空邮箱, 满即丢弃 + 计数, 零等待(可在 TIM6 中断里安全调用)。
 *         ⚠ 不能用 HAL_GetTick 做超时: 发送在 TIM6(优先级2) 中断里调,
 *           而 SysTick(优先级15) 抢不进来, 超时永远不到期。
 * 单生产单消费(ISR 写 / 任务读), 无锁; 队列长度为 2 的幂, 用位与取模。
 */
#include "bsp_can.h"
#include "can.h"
#include "config_can.h"
#include <string.h>

/* ── 环形队列 ─────────────────────────────────────────────── */
typedef struct
{
    uint32_t id;
    uint8_t  data[8];
} can_frame_t;

static can_frame_t      s_q1[CAN1_RX_QUEUE_LEN];
static volatile uint16_t s_q1_head, s_q1_tail;   /* head=ISR 写入, tail=任务读出 */
static can_frame_t      s_q2[CAN2_RX_QUEUE_LEN];
static volatile uint16_t s_q2_head, s_q2_tail;

static volatile uint32_t s_can1_tx_drop, s_can1_rx_drop;
static volatile uint32_t s_can2_tx_drop, s_can2_rx_drop;

static void q_push(can_frame_t *q, volatile uint16_t *head, volatile uint16_t *tail,
                   uint16_t len_mask, volatile uint32_t *drop,
                   uint32_t id, const uint8_t *data)
{
    uint16_t next = (uint16_t)((*head + 1u) & len_mask);

    if (next == *tail)                  /* 队列满: 丢最新帧并计数 */
    {
        (*drop)++;
        return;
    }
    q[*head].id = id;
    memcpy(q[*head].data, data, 8u);
    *head = next;
}

static bool q_pop(can_frame_t *q, volatile uint16_t *head, volatile uint16_t *tail,
                  uint16_t len_mask, uint32_t *id, uint8_t data[8])
{
    if (*tail == *head)                 /* 空 */
    {
        return false;
    }
    *id = q[*tail].id;
    memcpy(data, q[*tail].data, 8u);
    *tail = (uint16_t)((*tail + 1u) & len_mask);
    return true;
}

/* ── 过滤器 ───────────────────────────────────────────────── */

/* 32 位 MASK 模式: 标准 ID 位于 FilterIdHigh 的高 11 位, 低 3 位由掩码置 0 即"不关心" */
static void filter_config(CAN_HandleTypeDef *hcan, uint32_t bank,
                          uint32_t id_high, uint32_t mask_high)
{
    CAN_FilterTypeDef f = {0};

    f.FilterBank           = bank;
    f.FilterMode           = CAN_FILTERMODE_IDMASK;
    f.FilterScale          = CAN_FILTERSCALE_32BIT;
    f.FilterIdHigh         = (uint16_t)id_high;
    f.FilterIdLow          = 0u;
    f.FilterMaskIdHigh     = (uint16_t)mask_high;
    f.FilterMaskIdLow      = 0u;
    f.FilterFIFOAssignment = CAN_RX_FIFO0;
    f.FilterActivation     = ENABLE;
    f.SlaveStartFilterBank = 14u;       /* CAN1 用 bank 0~13, CAN2 用 14~27 */
    (void)HAL_CAN_ConfigFilter(hcan, &f);
}

/* ── 对外接口 ─────────────────────────────────────────────── */

bool bsp_can_init(void)
{
    filter_config(&hcan1, 0u,  CAN1_FILTER_ID_HIGH, CAN1_FILTER_MASK_HIGH);
    filter_config(&hcan2, 14u, CAN2_FILTER_ID_HIGH, CAN2_FILTER_MASK_HIGH);

    if (HAL_CAN_Start(&hcan1) != HAL_OK) return false;
    if (HAL_CAN_Start(&hcan2) != HAL_OK) return false;

    if (HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return false;
    if (HAL_CAN_ActivateNotification(&hcan2, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) return false;

    return true;
}

static bool can_send(CAN_HandleTypeDef *hcan, uint32_t id,
                     const uint8_t data[8], volatile uint32_t *drop)
{
    CAN_TxHeaderTypeDef h;
    uint32_t mailbox;

    if (HAL_CAN_GetTxMailboxesFreeLevel(hcan) == 0u)   /* 3 邮箱全满 → 丢, 不等 */
    {
        (*drop)++;
        return false;
    }

    h.StdId = id;
    h.ExtId = 0u;
    h.IDE   = CAN_ID_STD;
    h.RTR   = CAN_RTR_DATA;
    h.DLC   = 8u;
    h.TransmitGlobalTime = DISABLE;

    if (HAL_CAN_AddTxMessage(hcan, &h, (uint8_t *)data, &mailbox) != HAL_OK)
    {
        (*drop)++;
        return false;
    }
    return true;
}

bool bsp_can1_send(uint32_t id, const uint8_t data[8])
{
    return can_send(&hcan1, id, data, &s_can1_tx_drop);
}

bool bsp_can2_send(uint32_t id, const uint8_t data[8])
{
    return can_send(&hcan2, id, data, &s_can2_tx_drop);
}

bool bsp_can1_pop(uint32_t *id, uint8_t data[8])
{
    return q_pop(s_q1, &s_q1_head, &s_q1_tail,
                 CAN1_RX_QUEUE_LEN - 1u, id, data);
}

bool bsp_can2_pop(uint32_t *id, uint8_t data[8])
{
    return q_pop(s_q2, &s_q2_head, &s_q2_tail,
                 CAN2_RX_QUEUE_LEN - 1u, id, data);
}

uint32_t bsp_can1_tx_drop_count(void) { return s_can1_tx_drop; }
uint32_t bsp_can2_tx_drop_count(void) { return s_can2_tx_drop; }
uint32_t bsp_can1_rx_drop_count(void) { return s_can1_rx_drop; }
uint32_t bsp_can2_rx_drop_count(void) { return s_can2_rx_drop; }

/* ── RX FIFO0 中断: 只做 GetRxMessage + 入队 ───────────────── */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
    CAN_RxHeaderTypeDef hdr;
    uint8_t data[8];

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &hdr, data) != HAL_OK)
    {
        return;
    }

    if (hcan->Instance == CAN1)
    {
        q_push(s_q1, &s_q1_head, &s_q1_tail, CAN1_RX_QUEUE_LEN - 1u,
               &s_can1_rx_drop, hdr.StdId, data);
    }
    else if (hcan->Instance == CAN2)
    {
        q_push(s_q2, &s_q2_head, &s_q2_tail, CAN2_RX_QUEUE_LEN - 1u,
               &s_can2_rx_drop, hdr.StdId, data);
    }
    else
    {
        /* 不应发生 */
    }
}
