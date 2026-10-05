/**
 * @file bsp_can.c
 * @brief P0 骨架: 接口就位, 实现待 P4(过滤器/队列/发送/计数)。
 *        骨架期 send 恒失败、pop 恒空, 不影响编译与调度联调。
 */
#include "bsp_can.h"
#include "main.h"
#include "config_can.h"

bool bsp_can_init(void)
{
    /* TODO P4.1: CAN1/CAN2 各配 MASK 过滤器(CAN1=bank0 收0x160/161,
       CAN2=bank14 收0x200~207) + HAL_CAN_Start + ActivateNotification(RX_FIFO0) */
    return true;
}

bool bsp_can1_send(uint32_t id, const uint8_t data[8])
{
    (void)id; (void)data;
    /* TODO P4.3: 查一次空邮箱, 满即丢弃+计数; 空→AddTxMessage */
    return false;
}

bool bsp_can2_send(uint32_t id, const uint8_t data[8])
{
    (void)id; (void)data;
    /* TODO P4.3 */
    return false;
}

bool bsp_can1_pop(uint32_t *id, uint8_t data[8])
{
    (void)id; (void)data;
    /* TODO P4.2: 从 CAN1 队列取一帧(0x160/0x161) */
    return false;
}

bool bsp_can2_pop(uint32_t *id, uint8_t data[8])
{
    (void)id; (void)data;
    /* TODO P4.2: 从 CAN2 队列取一帧(0x201~0x204) */
    return false;
}

uint32_t bsp_can1_tx_drop_count(void) { return 0; }
uint32_t bsp_can2_tx_drop_count(void) { return 0; }
