/**
 * @file bsp_can.h
 * @brief CAN 双总线收发 —— CAN1=主从互联(从机灯板), CAN2=电机控制
 *
 * 发送: 查一次空邮箱, 满即丢弃+计数, 零等待(可在 TIM6 中断里安全调用)。
 * 接收: RX 中断只入队(按 hcan 分发到对应总线队列), 业务侧 pop 消费。
 * ⚠ bsp 层只用 CAN1/CAN2 硬件名, 不出现 buff/motor 业务词。
 */
#ifndef BSP_CAN_H
#define BSP_CAN_H

#include <stdint.h>
#include <stdbool.h>

bool     bsp_can_init(void);                    /* 过滤器+Start+RX通知(CAN1/CAN2) */
bool     bsp_can1_send(uint32_t id, const uint8_t data[8]);   /* 从机总线 */
bool     bsp_can2_send(uint32_t id, const uint8_t data[8]);   /* 电机总线 */
bool     bsp_can1_pop(uint32_t *id, uint8_t data[8]);         /* 从机帧队列 */
bool     bsp_can2_pop(uint32_t *id, uint8_t data[8]);         /* 电机反馈队列 */
uint32_t bsp_can1_tx_drop_count(void);
uint32_t bsp_can2_tx_drop_count(void);

#endif /* BSP_CAN_H */
