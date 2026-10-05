/**
 * @file motor.h
 * @brief 电机控制 —— 双环(位置/速度)PID + 大符正弦前馈 + 编码器过零(CAN2: 0x200/0x201~204)
 *
 * 迁移自 2025 老主机 `motor_control()` / `PID_Calc()` / `Zero_Check()` / `CAN_Motor_Send1()`,
 * 公式与参数原值保留(见 config_motor.h); 差异仅三点:
 *   ① dt 由 TIM6 节拍给定恒定 0.001(老代码靠 DWT 实测补偿主循环抖动);
 *   ② 反馈统一走 bsp_can2_pop(老代码在 CAN 中断里直接解析);
 *   ③ 老的 `ChassisMotorCanReceive[i]` 里 i 是个全局变量(靠它恰好为 0 才取到电机0),
 *      这里显式用索引 0。
 */
#ifndef MOTOR_H
#define MOTOR_H

#include <stdbool.h>
#include <stdint.h>

void motor_init(void);
void motor_set_run(bool run);            /* SW_RUN(停/转) 拨码 → 目标是否推进 */
void motor_set_mode(bool big);           /* SW_BUFF(大小符) 拨码 */
void motor_update(float dt);             /* 1ms(TIM6中断): 排空反馈 → 给定 → 双环 → 0x200 */
void motor_on_feedback(uint32_t id, const uint8_t raw[8]);   /* 单帧反馈(0x201~0x204) */

#endif /* MOTOR_H */
