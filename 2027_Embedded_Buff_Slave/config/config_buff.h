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

#endif /* CONFIG_BUFF_H */
