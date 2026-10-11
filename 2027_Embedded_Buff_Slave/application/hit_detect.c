/**
 * @file hit_detect.c
 * @brief 击打采集与环数判定(S4 完整实现) —— 仅从机构建
 *
 * 老从机在 EXTI 回调里"当场判定并改业务状态"(老 gpio.c HAL_GPIO_EXTI_Callback:
 * once/start/start_countt 三个隐式标志 + 直接写 state), 新实现拆为:
 *   ISR: 只把 {引脚, 时间戳(g_sched_tick_cnt)} 记进单生产/单消费环形缓冲;
 *   4Hz task: 查环数表判定 → 置事件 → 状态机(S5) take 消费。
 *
 * 三条硬约束(《从机重构清单》§3.4, 违反会把一次正常击打成"打错复位"清掉整轮):
 *   ① 硬件沿已收敛为 IT_FALLING(.ioc+gpio.c 一致), 此处保留 HD_IGNORE_MS 时间窗作
 *      二道防线(回弹/同一击打的相邻电极抖动不判打错);
 *   ② 不在 hit_map[] 里的引脚(杂散回调)只计数并返回, 绝不参与判定
 *      (老代码的 else 分支把一切多余回调判成打错, 靠 400ms 全局锁存才没炸);
 *   ③ 打错 = 状态未武装 && 距上一沿 > HD_IGNORE_MS(**任何引脚**超窗都是打错候选)——
 *      与老代码锁存期外的判定一致(老判定从不看引脚)。窗内一律吞 = 同引脚回弹/相邻电极
 *      抖动的二道防线本来就够, 不需要再看引脚。
 *      (2026-10-10 验收修正: 初版把"引脚≠上次击发引脚"设为永久条件 —— 同一环的再次
 *       击打将永远判不进打错, 台架⑩"已激活/休眠板被击"若打在激活的那一圈上就无上报,
 *       故移除; 引脚信息仍记在 g_dbg_hit.last_pin 供观测。)
 *
 * 环数口径 = 2025 老从机原值: RDET1~8 → 10~3 环; ⚠ RDET9/RDET10 都写 1 环
 *   (2 环无对应, 老代码疑为笔误) —— 标定表唯一出处就是下面 s_hit_map[],
 *   S7 组 2 台架逐一短接实测后只改这一处。
 *
 * 与老代码的有意差异(已评审): 老 start 锁存 ~400ms 会吞掉窗内**一切**后续沿(含本板第二次
 *   真击); 新实现窗缩到 HD_IGNORE_MS=100ms, 超窗一律判打错 → 100~400ms 区间的第二环击打
 *   新会报打错、老被吞(差异表 #19, S7 组 3/⑩ 台架留意; 窗值 config 一处改)。
 */
#include "hit_detect.h"
#include "main.h"          /* KEY1~KEY10_Pin */
#include "config_buff.h"   /* HD_IGNORE_MS */

extern volatile uint32_t g_sched_tick_cnt;   /* stm32f4xx_it.c: TIM6 每 1ms +1(当时钟当秒表用) */

/* ── 引脚→环数 标定表(老从机口径; 引脚与老 RDET1~10 一一对应: PA1~5 / PC6~10) ── */
typedef struct
{
    uint16_t pin;
    uint8_t  circle;
} hd_map_t;

static const hd_map_t s_hit_map[] = {
    { KEY1_Pin,  10u },
    { KEY2_Pin,   9u },
    { KEY3_Pin,   8u },
    { KEY4_Pin,   7u },
    { KEY5_Pin,   6u },
    { KEY6_Pin,   5u },
    { KEY7_Pin,   4u },
    { KEY8_Pin,   3u },
    { KEY9_Pin,   1u },  /* ⚠ 老代码=1 环(RDET8=3、RDET10=1 → 2 环无对应, 疑为笔误)
                            待 S7 组 2 短接实测定案, 只改本表一处 */
    { KEY10_Pin,  1u },
};
#define HD_MAP_NUM  (sizeof(s_hit_map) / sizeof(s_hit_map[0]))

/* ── ISR 记录环(单生产=EXTI / 单消费=task; 长度为 2 的幂, 位与取模) ── */
#define HD_REC_LEN  8u
static volatile struct
{
    uint16_t pin;
    uint32_t tick;
} s_rec[HD_REC_LEN];
static volatile uint8_t s_rec_head, s_rec_tail;

/* ── 判定状态(显式化老 once/start 隐式标志) ── */
static volatile bool    s_armed;        /* 仅 SLV_ARMED 期间为 true(状态机驱动) */
static bool             s_fired;        /* 本次武装已击发(= 老 once==1), set_armed(true) 清零 */
static uint32_t         s_prev_edge_tick;  /* 上一个被记录的沿的时间(去抖窗基准) */
static bool             s_any_edge;        /* 上电以来记录过任何沿 */

/* 事件单槽: 产生于 task, 由状态机 take 消费; 槽被占时新事件丢弃并计数(正常流程不会满) */
static volatile hd_event_t s_ev;
static uint8_t          s_ev_circle;
static uint16_t         s_ev_pin;

/* ── 调试快照(长期保留, Watch g_dbg_hit; S7 组 2 的主判据) ──────────────
 *   fire_cnt/mishit_cnt : 有效击发/打错次数
 *   last_pin/last_circle: 最近一次事件(环数只对 HIT 有意义)
 *   ignored             : 窗内被吞的沿(回弹/相邻电极抖动; 二道防线在起作用)
 *   stray               : 不在标定表里的杂散回调(必须恒为 0, 否则 EXTI 配置有问题)
 *   rec_over/ev_lost    : 记录环满丢样 / 事件槽占用丢事件 —— 都应恒 0
 *   age_ms              : 距最近一个沿多少 ms(短接后应立刻变小)            */
volatile struct
{
    uint32_t edges;
    uint32_t fire_cnt;
    uint32_t mishit_cnt;
    uint32_t ignored;
    uint32_t stray;
    uint32_t rec_over;
    uint32_t ev_lost;
    uint16_t last_pin;
    uint8_t  last_circle;
    uint8_t  armed;
    uint8_t  st;          /* hd_st_t */
    uint8_t  ev;          /* hd_event_t: 当前待消费事件 */
    uint32_t age_ms;
} g_dbg_hit;

/* ── 对外接口 ─────────────────────────────────────────────── */

void hit_detect_init(void)
{
    s_armed = false;
    s_fired = false;
    s_prev_edge_tick = 0u;
    s_any_edge = false;
    s_rec_head = s_rec_tail = 0u;
    s_ev = HD_EV_NONE;
}

void hit_detect_set_armed(bool armed)
{
    s_armed = armed;
    if (armed)
    {
        s_fired = false;    /* 重新武装 = 老代码"进休眠清 once": 一次点亮只许击中一次 */
    }
}

hd_event_t hit_detect_take(uint8_t *circle, uint16_t *pin)
{
    hd_event_t ev = s_ev;

    if (ev == HD_EV_NONE)
    {
        return ev;
    }
    s_ev = HD_EV_NONE;
    *circle = s_ev_circle;
    *pin    = s_ev_pin;
    return ev;
}

/* ── 4Hz: 排空记录环 → 逐沿判定 → 置事件 ──────────────────── */
void hit_detect_task(void)
{
    while (s_rec_tail != s_rec_head)
    {
        uint16_t pin  = s_rec[s_rec_tail].pin;
        uint32_t tick = s_rec[s_rec_tail].tick;
        uint32_t gap  = s_any_edge ? (tick - s_prev_edge_tick) : (HD_IGNORE_MS + 1u);
        uint8_t  idx;

        s_prev_edge_tick = tick;
        s_any_edge       = true;
        s_rec_tail = (uint8_t)((s_rec_tail + 1u) & (HD_REC_LEN - 1u));

        /* 查环数(回调已滤过表外引脚, 这里必然命中; 防御性保留) */
        for (idx = 0u; idx < HD_MAP_NUM; idx++)
        {
            if (s_hit_map[idx].pin == pin) break;
        }
        if (idx >= HD_MAP_NUM)
        {
            g_dbg_hit.stray++;
            continue;
        }

        if (s_armed && !s_fired)
        {
            /* ── 有效击发(老 once==0 && state==2) ── */
            s_fired = true;
            if (s_ev == HD_EV_NONE)
            {
                s_ev = HD_EV_HIT;
                s_ev_circle = s_hit_map[idx].circle;
                s_ev_pin    = pin;
                g_dbg_hit.last_pin    = pin;
                g_dbg_hit.last_circle = s_hit_map[idx].circle;
                g_dbg_hit.fire_cnt++;
            }
            else
            {
                g_dbg_hit.ev_lost++;      /* 槽被占: 理论上个 tick 已被消费, 不应发生 */
            }
            continue;
        }

        /* ── 未武装/已击发: 超窗任何沿都是打错候选(硬约束③, 2026-10-10 验收修正) ── */
        if (gap > (uint32_t)HD_IGNORE_MS)
        {
            if (s_ev == HD_EV_NONE)
            {
                s_ev = HD_EV_MISHIT;
                s_ev_circle = 0u;
                s_ev_pin    = pin;
                g_dbg_hit.last_pin = pin;
                g_dbg_hit.mishit_cnt++;
            }
            else
            {
                g_dbg_hit.ev_lost++;
            }
        }
        else
        {
            g_dbg_hit.ignored++;          /* 同引脚回弹 / 窗内相邻沿: 老代码用 400ms 全局锁存吞 */
        }
    }

    /* 观测快照 */
    g_dbg_hit.armed = s_armed ? 1u : 0u;
    g_dbg_hit.ev    = (uint8_t)s_ev;
    if (s_ev == HD_EV_MISHIT)
    {
        g_dbg_hit.st = HD_ST_MISHIT;
    }
    else if (s_armed)
    {
        g_dbg_hit.st = s_fired ? HD_ST_FIRED : HD_ST_ARMED;
    }
    else
    {
        g_dbg_hit.st = HD_ST_LOCKED;
    }
    g_dbg_hit.age_ms = s_any_edge ? (g_sched_tick_cnt - s_prev_edge_tick) : 0xFFFFFFFFu;
}

/* ── EXTI 回调(覆盖 HAL 弱符号; HAL_GPIO_EXTI_IRQHandler 已清 pending, 无需手动清) ──
 * 10 路 KEY 分布在 EXTI1/2/3/4/9_5/15_10 六个中断向量里, EXTI9_5 一次会给多个线号
 * 调本回调 → 表外引脚必须只计数返回(硬约束②)。 */
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    uint8_t i;
    uint8_t next;

    for (i = 0u; i < HD_MAP_NUM; i++)
    {
        if (s_hit_map[i].pin == pin) break;
    }
    if (i >= HD_MAP_NUM)
    {
        g_dbg_hit.stray++;
        return;
    }

    next = (uint8_t)((s_rec_head + 1u) & (HD_REC_LEN - 1u));
    if (next == s_rec_tail)
    {
        g_dbg_hit.rec_over++;             /* 环满丢最新沿: 4Hz 消费速度下不应发生 */
        return;
    }
    s_rec[s_rec_head].pin  = pin;
    s_rec[s_rec_head].tick = g_sched_tick_cnt;
    s_rec_head = next;
    g_dbg_hit.edges++;
}
