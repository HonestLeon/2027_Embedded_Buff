/**
 * @file config_can.h
 * @brief CAN 双总线 ID / 过滤器 / 发送策略 —— 位率参数在 CubeMX 生成的 can.c 里,
 *        不在此重复(同一参数只允许一个真相源)。
 *
 * 总线分工(接线事实): CAN1=主从互联(从机灯板), CAN2=电机控制。
 * ID 语义为 2025 赛季老协议原值, 逐字节兼容是硬约束。
 */
#ifndef CONFIG_CAN_H
#define CONFIG_CAN_H

/* ── ID 表(旧协议原值) ────────────────────────────────────── */
#define CAN_ID_MASTER_TO_SLAVE   0x161u   /* 主机发: 点灯/熄灭/全亮/复位指令 */
#define CAN_ID_SLAVE_TO_MASTER   0x160u   /* 从机发: 击打回报/打错复位       */
#define CAN_ID_MOTOR_TX          0x200u   /* 主机发: 电机电流帧(CAN2)        */
#define CAN_ID_MOTOR_FB_BASE     0x201u   /* 电机反馈 0x201~0x204 (CAN2)     */

/* ── 过滤器(MASK 模式, 两条总线各自独立一套) ─────────────────
 * 多收一两个空隙 ID 无害; CAN2 的 FilterBank 必须 >= SlaveStartFilterBank(=14) */
#define CAN1_FILTER_ID_HIGH      ((0x160u)<<5)   /* CAN1 收 0x160~0x167(实际仅0x160/161) */
#define CAN1_FILTER_MASK_HIGH    ((0x7F8u)<<5)
#define CAN2_FILTER_ID_HIGH      ((0x200u)<<5)   /* CAN2 收 0x200~0x207(电机反馈0x201~204) */
#define CAN2_FILTER_MASK_HIGH    ((0x7F8u)<<5)

/* ── 接收队列深度(bsp_can.c 实现) ──
 * CAN1: 从机帧 4Hz, 16 帧足够;
 * CAN2: 电机反馈 1kHz×4 ID ≈ 4 帧/ms, 由 motor_update 每 1ms 排空, 32 帧留 8 倍裕量。
 * 长度必须是 2 的幂(实现用位与取模), 改这里要同步改 bsp_can.c 的数组大小检查。 */
#define CAN1_RX_QUEUE_LEN        16u
#define CAN2_RX_QUEUE_LEN        32u

/* ── 发送策略: 查一次空邮箱, 满即丢弃+计数, 零等待(策略在 bsp_can.c 实现) ──
 * 不能用 HAL_GetTick 做超时: 发送在 TIM6 中断里调, SysTick(优先级15)
 * 抢不进 TIM6(优先级2), 超时永远不到期。满即丢的语义:
 * 邮箱满=硬件来不及发, 丢旧帧等下个周期的新数据反而更正确; drop 计数可观测。 */

#endif /* CONFIG_CAN_H */
