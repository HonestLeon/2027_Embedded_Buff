/**
 * @file config_motor.h
 * @brief 电机控制参数 —— 全部为 2025 老固件实测原值(老 motor_control()),
 *        调整前先确认台架表现。
 */
#ifndef CONFIG_MOTOR_H
#define CONFIG_MOTOR_H

#define MOTOR_SMALL_SPEED   (3.1415926f/3.0f)  /* 小符匀速角速度 rad/s (老π/3) */
#define MOTOR_BIG_A         1.0f               /* 大符角速度 v = A·sin(w·t)+B   */
#define MOTOR_BIG_W         1.9f               /*   老a=1, w=1.9, b=2.09-a      */
#define MOTOR_BIG_B         (2.09f - MOTOR_BIG_A)
#define MOTOR_FF_K          5000.0f            /* 前馈幅值(老forward_speed_k)   */
#define MOTOR_FF_PHASE      (-0.45f*3.1415926f)/* 前馈相位(老forward_speed_fai) */

#define MOTOR_POS_KP        200.0f             /* 位置环 P(老Motor1_Pid_Pos)    */
#define MOTOR_SPD_KP        15.0f              /* 速度环 P(老Motor1_Pid_Speed)  */
#define MOTOR_CUR_LIMIT     10000              /* 电流限幅(老LIMIT_MAX_MIN原值) */

#define MOTOR_ENC_CPR       8192               /* 编码器一圈计数(老Zero_Check)  */

#endif /* CONFIG_MOTOR_H */
