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

/* ── 机构比例(老 main.h 原值) ── */
#define MOTOR_CIRCLE_NUM    153.5f             /* 大符转一圈对应的电机圈数(老 CIRCLE_NUM)
                                                  ⚠ 位置环的单位基准, 台架实测复核后再动 */
#define MOTOR_RATO          3.0f               /* 齿轮比(老 RATO) */

/* ── 反馈槽位: 0x201~0x204 ── */
#define MOTOR_NUM           4u

/* ── 电流帧(0x200)发送周期(ms) ──
 * 老代码每个主循环都发(≈1ms); 控制环(位置/速度 PID + 过零)始终跑 1ms, 只有发帧受此限。
 * 总线容量核算(实测旧固件 CAN2 = 1Mbps): 8 字节帧 ≈110~135µs,
 *   1kHz 发帧 ≈13% 负载; 电调反馈 1kHz 再 ≈13% → 总 ≈26%, 余量充足 → 1ms 可行。
 * (若日后把 CAN2 降到 100kbps, 则需改大本值, 否则邮箱常满、drop 计数持续增长。) */
#define MOTOR_TX_PERIOD_MS  1u

#endif /* CONFIG_MOTOR_H */
