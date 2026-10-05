/**
 * @file motor.c
 * @brief P0 骨架: 电机控制待 P5(老 motor_control/PID/Zero_Check 参数与逻辑原样迁移)。
 */
#include "motor.h"
#include "config_motor.h"

void motor_init(void)
{
    /* TODO P5: PID 参数装载(MOTOR_POS_KP/MOTOR_SPD_KP) */
}

void motor_set_run(bool run)
{
    (void)run;
    /* TODO P5: SW_RUN → 目标是否推进(停转时电流清零=老行为) */
}

void motor_set_mode(bool big)
{
    (void)big;
    /* TODO P5: 大/小符 */
}

void motor_update(float dt)
{
    (void)dt;
    /* TODO P5.2: 排空 bsp_can2_pop → motor_on_feedback;
       正弦目标+前馈+双环PID+过零累计 → 限幅 → bsp_can2_send(0x200) */
}

void motor_on_feedback(uint8_t raw[8])
{
    (void)raw;
    /* TODO P5.1: 角度/转速解析(老MotorReceive原逻辑) + Zero_Check 过零 */
}
