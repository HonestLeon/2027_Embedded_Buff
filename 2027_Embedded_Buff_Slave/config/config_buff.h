/**
 * @file config_buff.h
 * @brief 从机灯板业务(大小符)的游戏规则时序 —— 老逻辑原值
 */
#ifndef CONFIG_BUFF_H
#define CONFIG_BUFF_H

#define BUFF_PANEL_NUM           5     /* 灯板数                              */
#define BUFF_BIG_ROUNDS          5     /* 大符激活双灯板次数                  */
#define BUFF_HIT_TIMEOUT_MS      2500u /* 无击打超时复位(老reset_t>=2.5)      */
#define BUFF_HIT_WINDOW_MS       1000u /* 大符首击后窗口(老first_hit_flag+wait_t) */
#define BUFF_EXTINGUISH_GAP_MS   250u  /* 熄灭旧对→点亮新对的间隔(1个逻辑帧)
   ⚠ 行为变更: 老代码此间隔≈1ms(HAL_GetTick差>0即过), 新架构为1个4Hz帧;
   如需老时序, 把熄灭→点亮的衔接放到 1ms 节拍处理, 勿在中断里死等 */
#define BUFF_RANDOM_ARRAY_LEN    5

/* ── P4 联调临时开关 ──
 * 1 = buff_step() 每 4Hz 发一帧 0x161(全复位) 并把 CAN1 收到的从机帧排空计数,
 *     用于 CAN 盒/从机联调验证收发链路(看 g_dbg_can1_rx_frames);
 * 0 = 关闭(P6 实现大小符状态机后置 0)。 */
#define BUFF_P4_CAN_TEST         0   /* ⚠ 从机保持 0: 该钩子会发主机帧(0x161), 从机应改为
                                        收 0x161 → 点灯, 发 0x160 上报击打 */

#endif /* CONFIG_BUFF_H */
