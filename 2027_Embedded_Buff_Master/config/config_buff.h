/**
 * @file config_buff.h
 * @brief 从机灯板业务(大小符)的游戏规则时序 —— 老逻辑原值
 */
#ifndef CONFIG_BUFF_H
#define CONFIG_BUFF_H

#define BUFF_PANEL_NUM           5     /* 灯板数                              */
#define BUFF_BIG_ROUNDS          5     /* 大符激活双灯板次数(=规则"5 组灯臂")   */
#define BUFF_HIT_TIMEOUT_MS      2500u /* 无击打超时复位(老reset_t>=2.5)      */
#define BUFF_HIT_WINDOW_MS       1000u /* 大符首击后窗口(老first_hit_flag+wait_t) */
#define BUFF_EXTINGUISH_GAP_MS   150u  /* 熄灭旧对 → 点亮新对的间隔(大符轮次切换)
 *
 * 规则约束(官方): "重新随机点亮下一组装甲模块前, 灯臂和装甲模块灯效将熄灭至多 200ms"
 *   → 上限 200ms; 取 150ms 留裕量, 同时让"旧对熄灭→新对点亮"肉眼可见。
 *
 * ⚠ 为什么不能取 1ms(老代码) 或 5ms:
 *   该间隔内主机要发两帧(先 0x01 熄灭旧对, 再 0x02 点亮新对), 从机必须**两帧都收到**
 *   才能把旧对熄掉。老从机是"单缓冲 + 200ms 主循环", 1ms 的两帧只会看到后一帧
 *   → 熄灭帧被吞 → 旧对不熄(这正是 2025 老固件实际表现为"不熄灭"的原因)。
 *   ⇒ 从机必须及时处理 CAN(建议 1ms 排空队列, 见从机模板 README 的"协议时序要求")。
 *   若用 200ms 单缓冲的从机与之对接, 熄灭仍会丢失(旧从机固有缺陷, 新从机已修复)。 */
#define BUFF_RANDOM_ARRAY_LEN    5

/* ── 大符随机可用组合数 = C(BUFF_PANEL_NUM, 2) ──
 * buff_random_big() 从这 C 种"两块组合"里**无重复**抽 BUFF_RANDOM_ARRAY_LEN 组,
 * 保证 5 轮不会出现同一对点两遍(详见 buff.c 的函数注释)。 */
#define BUFF_BIG_PAIR_NUM        (BUFF_PANEL_NUM * (BUFF_PANEL_NUM - 1) / 2)

#if BUFF_RANDOM_ARRAY_LEN > BUFF_BIG_PAIR_NUM
#error "大符轮数超过可用两两组合数 C(BUFF_PANEL_NUM,2), buff_random_big 无法保证各组互不相同"
#endif

/* ── 小符需要击中的次数 ──
 * 5 = 按规则(5 个灯板各一次)              ← 新实现默认
 * 6 = 复刻 2025 老固件的实际行为: 老代码 `num[counter]` 在 counter==5 时越界读
 *     (读到相邻数组 num2[0]), 导致实际会多点亮/多击打一次(重复点亮一个已打过的灯板)。
 * 置 6 时, 第 6 次会显式重新点亮 s_small[0](把越界读变成有定义的行为)。
 * ⚠ 改动会影响比赛规则判定, 上赛场前与队伍确认用哪个值。 */
#define BUFF_SMALL_HIT_COUNT     5u

/* ── 第一组(刚点亮的第一块/第一对)是否也受 2.5s 超时约束 ──
 * 0 = **第一块/第一组不计时**(无限等待第一次击中)   ← 当前设置(队伍确定)
 * 1 = 任何被点亮的装甲模块都受 2.5s 约束
 * 备注: 规则 5.5.2.1 的字面表述把"未能在 2.5 秒内击中点亮的装甲模块"列为激活失败条件
 *       (含第一块); 实际按 0 执行(与 2025 老固件一致), 如赛前裁判有异议再切 1。
 *       ⚠ 大符不看本项: BUFF_BIG_REFRESH_EN=1 时大符改用"周期刷新换一对", 不再超时复位。 */
#define BUFF_FIRST_HIT_TIMEOUT_EN 0u

/* ── 【仅主机】大符"没打中就换一对"(周期刷新) ──
 * 1 = 大符点亮一对后, 若 BUFF_BIG_REFRESH_MS 内**两块都没被打中** →
 *     熄灭旧对 → 隔 BUFF_EXTINGUISH_GAP_MS(150ms) → 点亮**新一对**。
 *     这是"刷新", 不算激活失败: 不推进 s_round(进度不变)、也不复位整局,
 *     一直接/换到任意一块被打中(→1s 窗口走原流程)或打错(→整局复位)为止。
 * 0 = 老行为: 大符第一对无限等待(配合 BUFF_FIRST_HIT_TIMEOUT_EN=0),
 *     第二对起 2.5s 无击打 → 整局复位。
 * 新一对从 C(5,2)=10 种组合的"领用池"里取 → 本局点过的每一对都不重样(同 M8 口径)。
 * ⚠ 小符不受本开关影响(仍是"2.5s 无击打复位", 第一块按 M6 不计时)。 */
#define BUFF_BIG_REFRESH_EN      1
#define BUFF_BIG_REFRESH_MS      2500u   /* 一个刷新周期(与老的 2.5s 击打时限同值) */

/* ── P4 联调临时开关 ──
 * 1 = buff_step() 每 4Hz 发一帧 0x161(全复位) 并把 CAN1 收到的从机帧排空计数,
 *     用于 CAN 盒/从机联调验证收发链路(看 g_dbg_can1_rx_frames);
 * 0 = 关闭(P6 实现大小符状态机后置 0)。 */
#define BUFF_P4_CAN_TEST         0

#endif /* CONFIG_BUFF_H */
