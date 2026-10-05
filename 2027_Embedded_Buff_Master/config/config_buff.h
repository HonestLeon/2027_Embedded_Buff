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
#define BUFF_EXTINGUISH_GAP_MS   5u    /* 熄灭旧对→点亮新对的间隔
   老代码≈1个主循环(≈1ms); 新架构由 buff_tick(1ms) 计时, 取 5ms 保持"1ms 级衔接"语义
   (对外表现与老代码一致: 旧对熄灭后立即点亮新对) */
#define BUFF_RANDOM_ARRAY_LEN    5

/* ── 小符需要击中的次数 ──
 * 5 = 按规则(5 个灯板各一次)              ← 新实现默认
 * 6 = 复刻 2025 老固件的实际行为: 老代码 `num[counter]` 在 counter==5 时越界读
 *     (读到相邻数组 num2[0]), 导致实际会多点亮/多击打一次(重复点亮一个已打过的灯板)。
 * 置 6 时, 第 6 次会显式重新点亮 s_small[0](把越界读变成有定义的行为)。
 * ⚠ 改动会影响比赛规则判定, 上赛场前与队伍确认用哪个值。 */
#define BUFF_SMALL_HIT_COUNT     5u

/* ── P4 联调临时开关 ──
 * 1 = buff_step() 每 4Hz 发一帧 0x161(全复位) 并把 CAN1 收到的从机帧排空计数,
 *     用于 CAN 盒/从机联调验证收发链路(看 g_dbg_can1_rx_frames);
 * 0 = 关闭(P6 实现大小符状态机后置 0)。 */
#define BUFF_P4_CAN_TEST         0

#endif /* CONFIG_BUFF_H */
