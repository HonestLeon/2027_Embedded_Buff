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
 *   ② 熄灭旧对→点亮新对的间隔: 老≈1个主循环(1ms), 新用 BUFF_EXTINGUISH_GAP_MS(5ms), 视觉等效。
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
