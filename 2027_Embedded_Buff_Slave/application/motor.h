/**
 * @file motor.h
 * @brief 电机控制 —— 双环PID + 大符正弦前馈 + 编码器过零(CAN2, 0x200/0x201~204)
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

void motor_init(void);
void motor_set_run(bool run);            /* SW_RUN 拨码 → 目标是否推进 */
void motor_set_mode(bool big);           /* 大/小符 */
void motor_update(float dt);             /* 1ms(TIM6中断): 排空CAN2反馈 → PID → 0x200 */
void motor_on_feedback(uint8_t raw[8]);  /* 单帧反馈解析(0x201~204) */

#endif /* MOTOR_H */
