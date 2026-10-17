/**
 * @file buff.c
 * @brief 从机灯板业务 —— CAN 协议 + 大小符状态机 + 随机序列(P6 完整实现)
 *
 * 与 2025 老主机 `master_work()` / `Decode()` 的对应:
 *   老的 6 个标志(finish_flag / first_hit_flag / wait_t / counter_start / reset_t /
 *   get_tick_flag) → 显式状态机 + 计时器(转换表见《代码架构设计》3.3)
 *
 * ⚠ 有意修正的 3 处(与老代码不同, 已核实):
 *   ① 老小符分支 `if (finish_flag && counter <= 5) ... num[counter]` 在 counter==5 时
 *      访问 num[5] **越界**(读到相邻数组 num2[0]), 使小符实际需要点亮/击中 **6 次**才全亮;
 *      新实现按规则只做 **5 次**(5 个灯板各一次)。
 *   ② 熄灭旧对→点亮新对的间隔: 老≈1个主循环(1ms), 新用 BUFF_EXTINGUISH_GAP_MS(150ms)。
 *      老间隔太短, 老从机 200ms 单缓冲只看到后一帧 → 熄灭帧被吞; 150ms 既满足规则
 *      "熄灭至多 200ms" 上限, 又留出从机处理两帧的余量(从机须 1ms 级排空 CAN)。
 *   ③ 中途切换 SW1(大小符): 新实现直接复位重新开始(老代码会用新模式的变量去接旧计数, 属缺陷)。
 *
 * ⚠ 刻意保留的老行为:
 *   - 第一块灯板点亮后**不设超时**(必须被击中才继续); 从第二块起 2.5s 无击打 → 复位;
 *   - 大符首击后有 1s 窗口, 之后才熄灭旧对并点亮新对;
 *   - 从机回报"打错复位"([2]==0x01) 优先于"被点亮"([1]==0x03) —— 因为从机的
 *     复位帧里 [1] 会残留上一次的 0x03(老协议两个方向共用一帧结构), 老代码也是先判后者。
 */
#include "buff.h"
#include <stdlib.h>          /* rand() */
#include "config_buff.h"
#include "config_can.h"
#include "bsp_can.h"
#include "bsp_switch.h"
#include "config_board.h"   /* BOARD_ROLE_MASTER / SLAVE_PANEL_NUM: 状态机段按角色编译 */
#if !BOARD_ROLE_MASTER
#include "light.h"          /* 场景渲染(S6 图案库) */
#include "hit_detect.h"     /* 击打事件(S4) */
#endif

/* ── 协议字节位置 ─────────────────────────────────────────── */
#define BF_HEAD                      0xAAu   /* 包头 */
#define BF_LEN                       8u
#define BF_IDX_HEAD                  0u
#define BF_IDX_CMD                   1u
#define BF_IDX_TYPE                  2u
#define BF_IDX_PANEL_A               3u
#define BF_IDX_COLOR                 4u
#define BF_IDX_PANEL_B               5u
#define BF_IDX_ROUND                 6u
#define BF_IDX_RESERVED              7u

/* 从机回传帧里的两个语义位(老协议: 与主机下行帧共用同一结构) */
#define BF_RPT_HIT_BYTE              1u      /* [1]==0x03: 被点亮/被击中 */
#define BF_RPT_HIT_VALUE             0x03u
#define BF_RPT_RESET_BYTE            2u      /* [2]==0x01: 打错 → 从机自己复位 */
#define BF_RPT_RESET_VALUE           0x01u

/* ── 调试观察量(长期保留) ─────────────────────────────────── */
volatile uint32_t g_dbg_can1_rx_frames = 0u;   /* 收到的从机帧累计数 */
volatile uint8_t  g_dbg_buff_state     = 0u;   /* 当前状态(bb_state_t) */
volatile uint8_t  g_dbg_buff_round     = 0u;   /* 当前轮次(已点亮次数) */

/* ══════════════════════ 协议: 主机 → 从机 ══════════════════════ */

uint8_t buff_frame_pack(uint8_t out[8], const buff_frame_t *f)
{
    out[BF_IDX_HEAD]     = BF_HEAD;
    out[BF_IDX_CMD]      = (uint8_t)f->cmd;
    out[BF_IDX_TYPE]     = f->is_big ? 0x01u : 0x00u;
    out[BF_IDX_PANEL_A]  = f->panel_a;
    out[BF_IDX_COLOR]    = f->is_blue ? 1u : 0u;
    out[BF_IDX_PANEL_B]  = f->panel_b;
    out[BF_IDX_ROUND]    = f->round;
    out[BF_IDX_RESERVED] = 0u;
    return BF_LEN;
}

bool buff_frame_parse(const uint8_t in[8], buff_frame_t *f)
{
    if (in[BF_IDX_HEAD] != BF_HEAD)
    {
        return false;
    }
    if (in[BF_IDX_CMD] < (uint8_t)BF_EXTINGUISH ||
        in[BF_IDX_CMD] > (uint8_t)BF_ALL_ON)
    {
        return false;
    }
    if (in[BF_IDX_PANEL_A] > BUFF_PANEL_NUM || in[BF_IDX_PANEL_B] > BUFF_PANEL_NUM)
    {
        return false;
    }

    f->cmd     = (bf_cmd_t)in[BF_IDX_CMD];
    f->is_big  = (in[BF_IDX_TYPE] == 0x01u) ? 1u : 0u;
    f->panel_a = in[BF_IDX_PANEL_A];
    f->panel_b = in[BF_IDX_PANEL_B];
    f->is_blue = (in[BF_IDX_COLOR] != 0u) ? 1u : 0u;
    f->round   = in[BF_IDX_ROUND];
    return true;
}

bool buff_send_frame(const buff_frame_t *f)
{
    uint8_t raw[BF_LEN];

    (void)buff_frame_pack(raw, f);
    return bsp_can1_send(CAN_ID_MASTER_TO_SLAVE, raw);
}

/* ══════════════════════ 协议: 从机 → 主机 ══════════════════════ */

bool buff_slave_report_parse(const uint8_t in[8], slave_report_t *r)
{
    if (in[BF_IDX_HEAD] != BF_HEAD)
    {
        return false;
    }
    /* 复位优先: 从机的复位帧里 [1] 会残留上一次的 0x03(老协议共用帧结构) */
    if (in[BF_RPT_RESET_BYTE] == BF_RPT_RESET_VALUE)
    {
        r->kind  = SLAVE_RPT_RESET;
        r->panel = 0u;
        return true;
    }
    if (in[BF_RPT_HIT_BYTE] == BF_RPT_HIT_VALUE)
    {
        uint8_t panel = in[BF_IDX_PANEL_A];

        if (panel < 1u || panel > BUFF_PANEL_NUM)
        {
            return false;
        }
        r->kind  = SLAVE_RPT_HIT;
        r->panel = panel;
        return true;
    }
    return false;
}

/* ══════════════════════ 随机序列(老算法原样, 去 srand 化) ══════════
 * 老函数内部有 srand(CNT_my)/srand(random_seed), 而 CNT_my 在新架构无人递增、
 * TIM2 已删 → 这里只保留 rand() 调用, 种子由上电时 bsp_rng_get_seed() 注入一次。 */

/* 老 generateUniqueRandomArray_SMALL_BUFF: 生成 length 个 [min,max] 内互不重复的数 */
void buff_random_small(uint8_t arr[], uint8_t length, uint8_t min, uint8_t max)
{
    for (uint8_t i = 0; i < length; i++)
    {
        uint8_t value;
        bool    unique;

        do
        {
            unique = true;
            value  = (uint8_t)(min + (uint8_t)(rand() % (int)(max - min + 1)));
            for (uint8_t j = 0; j < i; j++)
            {
                if (arr[j] == value)
                {
                    unique = false;
                    break;
                }
            }
        } while (!unique);

        arr[i] = value;
    }
}

/* 老 generateUniqueRandomArray_BIG_BUFF:
 * arr1 允许任意重复(大符前后两次的灯臂可以一样); arr2[i] 只要求与 arr1[i] 不同 */
void buff_random_big(uint8_t arr1[], uint8_t arr2[], uint8_t length, uint8_t min, uint8_t max)
{
    for (uint8_t i = 0; i < length; i++)
    {
        arr1[i] = (uint8_t)(min + (uint8_t)(rand() % (int)(max - min + 1)));
    }

    for (uint8_t i = 0; i < length; i++)
    {
        uint8_t value;
        do
        {
            value = (uint8_t)(min + (uint8_t)(rand() % (int)(max - min + 1)));
        } while (value == arr1[i]);

        arr2[i] = value;
    }
}

/* ═════════════════ S3.1 从机接收路径：1ms 排空 → 目标状态幂等覆盖 ══════════════
 * 为什么必须 1ms：大符轮次切换时主机先发 0x01(熄灭旧对)、150ms 后才发 0x02(点亮新对)。
 * 4Hz 才处理就会让后一帧覆盖前一帧 → 旧灯板不熄（老从机 200ms 单缓冲的老毛病，实测就是
 * "熄灭不稳定"）。本函数只做"收帧→解析→覆盖目标状态+置事件"，不动灯效、不发上报 ——
 * 那些留在 4Hz 逻辑帧与 S5 的从机状态机里。主机版构建不调用它（主机发 0x161、收 0x160）。
 *
 * 观测（调试器 Watch g_buff_rx）：
 *   frames/bad_frames  有效帧与废帧计数；seq      每收到一帧 +1，S5 用它判"有新事件"；
 *   cmd/is_big/...     最近一帧解析出的目标状态（幂等覆盖，与老代码"最后写入者赢"等价）；
 *   age_ms             距最近一帧多少 ms；max_burst 单次 poll 一口气排空到的最大帧数
 *                      （主机"全亮"会一次连发 5 帧，那一次 burst=5 属正常；持续 >1 才说明排空慢了）。 */

volatile buff_rx_t g_buff_rx;


void buff_can_rx_poll(void)
{
    uint32_t       id;
    uint8_t        raw[8];
    buff_frame_t   f;
    uint8_t        burst = 0u;

    if (g_buff_rx.age_ms < 0xFFFFu) { g_buff_rx.age_ms++; }

    while (bsp_can1_pop(&id, raw))
    {
        if (id != CAN_ID_MASTER_TO_SLAVE)     /* 从机只认下行 0x161 */
        {
            continue;
        }
        if (buff_frame_parse(raw, &f))
        {
            g_buff_rx.cmd      = (uint8_t)f.cmd;   /* 幂等覆盖: 多帧连发也不会丢状态 */
            g_buff_rx.is_big   = f.is_big;
            g_buff_rx.is_blue  = f.is_blue;
            g_buff_rx.panel_a  = f.panel_a;
            g_buff_rx.panel_b  = f.panel_b;
            g_buff_rx.round    = f.round;
            g_buff_rx.age_ms   = 0u;
            g_buff_rx.frames++;
            g_buff_rx.seq++;
            burst++;

            /* S5: 逐帧锁存"与本板有关"的命令(老 Decode 的匹配规则:
               0x03/0x04 广播不看板号; 0x01/0x02 必须 [3]或[5]==SLAVE_PANEL_NUM)。
               ★差异表 #21 隐式熄灭: 大符的点亮帧本身就定义了"本轮该亮的集合",
                 不在这个集合里的板一律灭 —— 熄灭帧丢了也能在 150ms 后自愈。
               必须在这里(1ms、逐帧)做, 不能等 4Hz 看 cmd 镜像 —— 同一逻辑帧窗口内
               别人的帧后到就会吃掉发给本板的熄灭帧(老单缓冲缺陷的翻版)。 */
            {
                uint8_t ev = 0u;
                uint8_t implicit = 0u;
                if (f.cmd == BF_RESET || f.cmd == BF_ALL_ON)
                {
                    ev = (uint8_t)f.cmd;
                }
                else if (f.panel_a == (uint8_t)SLAVE_PANEL_NUM ||
                         f.panel_b == (uint8_t)SLAVE_PANEL_NUM)
                {
                    ev = (uint8_t)f.cmd;
                }
                else if (f.cmd == BF_LIGHT && f.is_big != 0u)
                {
                    /* 差异表 #21: 大符下"不在本轮这一对里" = 该灭。
                       仅大符: 小符打过的板要保持亮环数(累计显示)。 */
                    ev = (uint8_t)BF_EXTINGUISH;
                    implicit = 1u;      /* 计数留给 buff_tick: 只有"确实还亮着"才算漏帧 */
                }
                if (ev != 0u)
                {
                    g_buff_rx.my_ev_cmd  = ev;      /* 同窗多条: 后到覆盖 = 老代码"最后写入者赢" */
                    g_buff_rx.my_ev_valid = 1u;
                    g_buff_rx.my_ev_implicit = implicit;
                    g_buff_rx.my_ev_seq++;
                }
            }
        }
        else
        {
            g_buff_rx.bad_frames++;          /* 包头/命令/板号范围不对: 只计数, 不动状态 */
        }
    }
    if (burst > g_buff_rx.max_burst)
    {
        g_buff_rx.max_burst = burst;
    }
}

/* ══════════════════════════ 以下整段 = 主机构建专用 ══════════════════════════
 * ⚠ 从机绝不能跑主机状态机: 它会每 250ms 往总线发 0x161(与真主机抢话)、上电就跑随机序列,
 *   还会在 4Hz 里 bsp_can1_pop —— 与上面 1ms 的排空抢同一队列。
 *   从机状态机(SLV_SLEEP/ARMED/ACTIVATED/ALL_ON)在 S5 落地; S3~S4 期间从机只收不发。 */
#if BOARD_ROLE_MASTER
/* ══════════════════════ 状态机(P6) ══════════════════════
 * ⚠【本段为"主机构建"专用】: 从机工程请把本节整段替换成从机状态机
 *   (休眠/待激活/已激活/全激活, 见《重构对照》第3节与从机模板 README)。
 *   上面"协议"与"随机序列"两节是主从共享的唯一真相源, 改动必须两边同步。 */

static bb_state_t s_state;
static bool       s_big;                                    /* 当前模式(SW1) */
static uint8_t    s_round;                                  /* 已点亮的轮次(0=未点亮) */
static uint8_t    s_small[BUFF_RANDOM_ARRAY_LEN];           /* 小符: 不重复序列 */
static uint8_t    s_big_a[BUFF_RANDOM_ARRAY_LEN];           /* 大符: 第一块 */
static uint8_t    s_big_b[BUFF_RANDOM_ARRAY_LEN];           /* 大符: 第二块 */
static float      s_t_state;                                /* 当前状态计时(s) */
static slave_report_t s_rpt;                                /* 本帧事件并暂存 */

/* ── 小工具 ── */
static bool color_is_blue(void)
{
    return (bsp_switch_get().sw_color != 0u);
}

static void send_cmd(bf_cmd_t cmd, uint8_t panel_a, uint8_t panel_b, uint8_t round)
{
    buff_frame_t f;

    f.cmd     = cmd;
    f.is_big  = s_big ? 1u : 0u;
    f.panel_a = panel_a;
    f.panel_b = panel_b;
    f.is_blue = color_is_blue() ? 1u : 0u;
    f.round   = round;
    (void)buff_send_frame(&f);
}

/* 帧内 [6]=round 的语义(规则"中部灯臂指示激活进度"):
 *   round = 已成功激活的组数(0~5), 从机据此显示 n/5 的进度条。
 *   例: 第 1 组点亮时 round=0; 第 1 组被击中后开始第 2 组时 round=1(进度 1/5)…全亮时 round=5(5/5)。 */
static void send_all_on(void)
{
    for (uint8_t p = 1u; p <= BUFF_PANEL_NUM; p++)
    {
        send_cmd(BF_ALL_ON, p, 0u, s_round);
    }
}

static void go(bb_state_t st)
{
    s_state   = st;
    s_t_state = 0.0f;
    g_dbg_buff_state = (uint8_t)st;
}

/* ── 进入复位: 重新取随机序列 + 发全复位帧(从机回休眠) ── */
static void do_reset(void)
{
    if (s_big)
    {
        buff_random_big(s_big_a, s_big_b, BUFF_RANDOM_ARRAY_LEN, 1u, (uint8_t)BUFF_PANEL_NUM);
    }
    else
    {
        buff_random_small(s_small, BUFF_RANDOM_ARRAY_LEN, 1u, (uint8_t)BUFF_PANEL_NUM);
    }
    s_round = 0u;
    send_cmd(BF_RESET, 0u, 0u, 0u);
    go(BB_IDLE);
}

/* ── 待激活 → 点亮第一组 ── */
static void do_idle(void)
{
    if (s_big)
    {
        send_cmd(BF_LIGHT, s_big_a[0], s_big_b[0], 0u);
    }
    else
    {
        send_cmd(BF_LIGHT, s_small[0], 0u, 0u);
    }
    s_round = 1u;
    g_dbg_buff_round = s_round;
    go(BB_LIT);
}

/* ── 当前轮回的目标灯板(供校验/上报匹配) ── */
static bool panel_in_current_target(uint8_t panel)
{
    if (s_big)
    {
        if (s_round < 1u)
        {
            return false;
        }
        return (panel == s_big_a[s_round - 1u]) || (panel == s_big_b[s_round - 1u]);
    }
    return (s_round >= 1u) && (panel == s_small[s_round - 1u]);
}

/* ── 收到"被点亮"回报 ── */
static void on_hit(uint8_t panel)
{
    if (s_state != BB_LIT)
    {
        return;                             /* 只在该状态下推进 */
    }
    if (!panel_in_current_target(panel))
    {
        return;                             /* 非当前目标的回报: 忽略(异常帧保护) */
    }

    if (s_big)
    {
        go(BB_HIT_WINDOW);                  /* 首击 → 进入 1s 窗口 */
    }
    else if (s_round >= (uint8_t)BUFF_SMALL_HIT_COUNT)
    {
        send_all_on();                      /* 全部打完 → 全亮 */
        go(BB_ALL_ON);
    }
    else
    {
        /* 点亮下一块; 若 BUFF_SMALL_HIT_COUNT 设为 6(复刻老行为), 第 6 次会按取模
           重新点亮已打过的灯板(老代码此处是越界读 num[5], 这里改成有定义的行为) */
        send_cmd(BF_LIGHT, s_small[s_round % BUFF_RANDOM_ARRAY_LEN], 0u, 0u);
        s_round++;
        g_dbg_buff_round = s_round;
        go(BB_LIT);
    }
}

/* ── 熄灭旧对(进入冷却) ── */
static void enter_cooldown(void)
{
    send_cmd(BF_EXTINGUISH, s_big_a[s_round - 1u], s_big_b[s_round - 1u], s_round);
    go(BB_COOLDOWN);
}

/* ── 冷却结束: 点亮新对 或 全亮 ── */
static void cooldown_done(void)
{
    if (s_round < (uint8_t)BUFF_BIG_ROUNDS)
    {
        send_cmd(BF_LIGHT, s_big_a[s_round], s_big_b[s_round], s_round);
        s_round++;
        g_dbg_buff_round = s_round;
        go(BB_LIT);
    }
    else
    {
        send_all_on();
        go(BB_ALL_ON);
    }
}

/* ── 排空 CAN1 报告队列 → 事件(4Hz 调用) ── */
static bool drain_report(void)
{
    uint32_t id;
    uint8_t  raw[8];
    bool     got = false;

    while (bsp_can1_pop(&id, raw))
    {
        g_dbg_can1_rx_frames++;
        if (id != CAN_ID_SLAVE_TO_MASTER)
        {
            continue;
        }
        if (buff_slave_report_parse(raw, &s_rpt))
        {
            got = true;
            break;                          /* 一帧一个事件; 其余留到下个 4Hz 处理 */
        }
    }
    return got;
}

/* ══════════════════════ 对外接口 ══════════════════════ */

void buff_init(void)
{
    s_big    = (bsp_switch_get().sw_buff != 0u);
    s_round  = 0u;
    s_t_state = 0.0f;
    go(BB_RESET);                           /* 第一次 buff_step 会发复位帧并进入 IDLE */
    g_dbg_buff_round = 0u;
}

void buff_step(void)
{
    /* ① 模式(SW1)中途变化 → 复位重来 */
    if (s_big != (bsp_switch_get().sw_buff != 0u))
    {
        s_big = (bsp_switch_get().sw_buff != 0u);
        go(BB_RESET);
    }

    /* ② 处理从机回报(每 4Hz 最多一个事件, 与老代码"一帧一次推进"一致) */
    if (drain_report())
    {
        if (s_rpt.kind == SLAVE_RPT_RESET)
        {
            go(BB_RESET);                   /* 打错复位: 任意状态都复位 */
        }
        else
        {
            on_hit(s_rpt.panel);
        }
    }

#if BUFF_P4_CAN_TEST
    /* P4 联调钩子(默认关闭): 4Hz 发一帧全复位, 便于 CAN 盒验证 CAN1 发送链路 */
    send_cmd(BF_RESET, 0u, 0u, 0u);
#endif

    /* ③ 状态机步进(发送动作) */
    switch (s_state)
    {
    case BB_RESET:      do_reset();            break;
    case BB_IDLE:       do_idle();             break;
    case BB_LIT:        /* 等待击中(事件驱动) */ break;
    case BB_HIT_WINDOW: /* 等待 1s 窗口(tick)  */ break;
    case BB_COOLDOWN:   /* 等待熄灭间隔(tick)  */ break;
    case BB_ALL_ON:     /* 等待 2.5s(tick)      */ break;
    default:                                   break;
    }
}

void buff_tick(float dt)
{
    s_t_state += dt;

    switch (s_state)
    {
    case BB_LIT:
        /* BUFF_FIRST_HIT_TIMEOUT_EN=0(当前设置): 第一块不设计时, 从第二块起 2.5s 无击打复位
           =1: 任何被点亮的装甲模块(含第一块)都受 2.5s 约束 */
        if ((BUFF_FIRST_HIT_TIMEOUT_EN || s_round >= 2u) &&
            s_t_state >= ((float)BUFF_HIT_TIMEOUT_MS / 1000.0f))
        {
            go(BB_RESET);
        }
        break;

    case BB_HIT_WINDOW:
        if (s_t_state >= ((float)BUFF_HIT_WINDOW_MS / 1000.0f))
        {
            enter_cooldown();
        }
        break;

    case BB_COOLDOWN:
        if (s_t_state >= ((float)BUFF_EXTINGUISH_GAP_MS / 1000.0f))
        {
            cooldown_done();
        }
        break;

    case BB_ALL_ON:
        if (s_t_state >= ((float)BUFF_HIT_TIMEOUT_MS / 1000.0f))
        {
            go(BB_RESET);
        }
        break;

    default:
        break;
    }
}

#else   /* ════════════════════ 从机状态机(S5) ════════════════════
 * 对照老从机 main.c + Decode():
 *   老 state 1/2/3/4          → SLV_SLEEP / SLV_ARMED / SLV_ACTIVATED / SLV_ALL_ON
 *   老 Decode() 板号匹配/广播  → buff_can_rx_poll() 已逐帧(1ms)锁存 my_ev, 本段只管消费
 *   老 "中断当场改状态"        → hit_detect 4Hz 事件 + 本段消费
 *   老 once(0/1/2)+start 锁存  → hit_detect 的 armed/fired + HD_IGNORE_MS
 * 有意修正(《从机重构清单》差异表):
 *   #8  上报帧四字节每次全写(不依赖上一帧 [1]/[3] 残留);
 *   #12 状态转换统一清屏(老 state4 不更新 last_state → 4→2 会残留环数图案)。
 * 时序纪律(《从机代码重构计划》S5 设计决定表):
 *   帧触发的转换在 1ms buff_tick() 消费锁存即时换态 —— 主机熄灭→点亮只隔 150ms,
 *   若等 4Hz 窗口, "先熄后亮"会塌缩成"只见点亮"(老单缓冲缺陷的另一半);
 *   击打事件消费与上报放 4Hz = 老主循环 ≈200ms 节拍, 不比老代码慢。 */

static slv_state_t s_slv = SLV_SLEEP;   /* 上电即休眠(老代码 state=1) */
static uint8_t     s_circle;            /* 本次 ACTIVATED 的环数(0=无) */

/* ── 调试快照(长期保留, Watch g_dbg_slv; S7 组 3 主判据) ─────
 *   state           : 0 休眠 / 1 待激活 / 2 已激活 / 3 全激活
 *   ev_cmd/ev_cnt   : 1ms 消费的"与本板有关"命令与次数(收帧触发的动作)
 *   hit_ev/mishit_ev: 4Hz 消费的击打事件; report_hit/report_mishit: 已发 0x160 帧数
 *     ⚠ 进入 ACTIVATED 那拍 report_hit 恰好 +1; 再打只涨 mishit_ev(不重复报)
 *   is_big/is_blue/round: [2]/[4]/[6] 镜像 —— round 当前只观测, 画 n/5 归 S8 */
volatile struct
{
    uint8_t  state;
    uint8_t  circle;
    uint8_t  ev_cmd;
    uint8_t  armed;
    uint32_t ev_cnt;
    uint32_t hit_ev;
    uint32_t mishit_ev;
    uint32_t report_hit;
    uint32_t report_mishit;
    uint8_t  is_big;
    uint8_t  is_blue;
    uint8_t  round;
} g_dbg_slv;

/* 上行 0x160 回报: 四字节全写(差异表 #8)。击中 [1]=0x03 / 打错 [2]=0x01,
 * 主机侧"复位优先"判定(与 buff_slave_report_parse 兼容)。 */
static void slv_report(bool mishit)
{
    uint8_t raw[8] = {0u};

    raw[BF_IDX_HEAD]       = BF_HEAD;
    raw[BF_RPT_HIT_BYTE]   = mishit ? 0x00u : BF_RPT_HIT_VALUE;
    raw[BF_RPT_RESET_BYTE] = mishit ? BF_RPT_RESET_VALUE : 0x00u;
    raw[BF_IDX_PANEL_A]    = (uint8_t)SLAVE_PANEL_NUM;
    if (bsp_can1_send(CAN_ID_SLAVE_TO_MASTER, raw))
    {
        if (mishit) { g_dbg_slv.report_mishit++; }
        else        { g_dbg_slv.report_hit++; }
    }
}

/* 状态转换唯一入口: 击打武装 + 场景切换(light_set_scene 是唯一清屏点)。
 * 只有"被击中"这一条路进 ACTIVATED(见 buff_step) → 上报天然只发一次。 */
static void slv_enter(slv_state_t st)
{
    s_slv = st;
    hit_detect_set_armed(st == SLV_ARMED);   /* 仅待激活允许被击中(老 once&&state==2 语义) */
    light_set_scene(st, s_circle);
    g_dbg_slv.state = (uint8_t)st;
    g_dbg_slv.armed = (st == SLV_ARMED) ? 1u : 0u;
}

void buff_init(void)
{
    s_slv    = SLV_SLEEP;
    s_circle = 0u;
    hit_detect_set_armed(false);
    g_dbg_slv.state = (uint8_t)s_slv;
    /* 灯带缓冲上电全 0 = 熄灭, 不重复清屏(light_init 在本函数之后才跑) */
}

/* 1ms: 消费"与本板有关"命令锁存 → 状态转换。
 * 已处于目标态再收同命令 = no-op(老 Decode 对 state 赋同值不触发清屏/重新武装)。 */
void buff_tick(float dt)
{
    slv_state_t tgt;
    uint8_t     cmd;
    uint8_t     impl;

    (void)dt;
    if (g_buff_rx.my_ev_valid == 0u)
    {
        return;
    }
    cmd = g_buff_rx.my_ev_cmd;
    impl = g_buff_rx.my_ev_implicit;
    g_buff_rx.my_ev_valid = 0u;
    g_buff_rx.my_ev_implicit = 0u;
    g_dbg_slv.ev_cmd = cmd;
    g_dbg_slv.ev_cnt++;

    if (cmd == (uint8_t)BF_LIGHT)
    {
        tgt = SLV_ARMED;
    }
    else if (cmd == (uint8_t)BF_ALL_ON)
    {
        tgt = SLV_ALL_ON;
    }
    else                                     /* BF_EXTINGUISH / BF_RESET: 一律回休眠 */
    {
        tgt = SLV_SLEEP;
        s_circle = 0u;
        if (impl != 0u && s_slv != SLV_SLEEP)
        {
            /* 差异表 #21 计数: 隐式熄灭却发现自己还亮着 = 显式熄灭帧漏过了 */
            g_buff_rx.imp_off++;
        }
    }
    if (tgt != s_slv)
    {
        slv_enter(tgt);
    }
}

/* 4Hz: 消费击打事件 → 上报 + 状态抬升; 颜色按帧更新(老 Decode 对任何 0xAA 帧都刷
 * color_type、图案每圈重画时读取 —— 差异表 #11 的"对所有板生效"语义)。 */
void buff_step(void)
{
    uint8_t    circle;
    uint16_t   pin;
    hd_event_t ev;

    light_set_color(g_buff_rx.is_blue != 0u);
    light_set_big(g_buff_rx.is_big != 0u);   /* 差异表 #20: 大符打中即熄灭灯臂 */
    g_dbg_slv.is_big  = g_buff_rx.is_big;
    g_dbg_slv.is_blue = g_buff_rx.is_blue;
    g_dbg_slv.round   = g_buff_rx.round;

    ev = hit_detect_take(&circle, &pin);
    if (ev == HD_EV_HIT)
    {
        s_circle = circle;
        slv_enter(SLV_ACTIVATED);   /* 唯一进入路径 → 进态上报恰好一次(老 state3 入口同) */
        slv_report(false);
        g_dbg_slv.circle  = circle;
        g_dbg_slv.hit_ev++;
    }
    else if (ev == HD_EV_MISHIT)
    {
        g_dbg_slv.mishit_ev++;
        slv_report(true);           /* 老顺序: 先发复位帧(once==2 分支), 再回休眠 */
        s_circle = 0u;
        slv_enter(SLV_SLEEP);
    }
}
#endif /* BOARD_ROLE_MASTER */