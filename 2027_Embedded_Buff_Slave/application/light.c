/**
 * @file light.c
 * @brief 从机灯板图案库(S6 完整实现) —— 纯复刻 2025 老从机 `Divice/WS2812.c` 的四种外观
 *
 * 口径纪律(《从机代码重构计划》S6 / 《从机重构清单》§3.5): **本阶段 = 纯复刻老从机**,
 * 老代码没画过的图案(进度条 n/5)一律不做, 归 S8; 三张标定表(LIGHT_ARMED_SEGS /
 * LIGHT_RING_MAP / LIGHT_ARROWHEAD_ROWS)数值一字未改, 只许搬位置不许改值。
 *
 * 外观 → 场景(函数对应关系):
 *   SLV_SLEEP     5 条全灭            (老 state1: 每圈清 5 条)
 *   SLV_ARMED     靶面 19 段 + 灯臂箭头流动 (老 state2: Buff_Activatable + Buff_Arrowhead_Flowing)
 *   SLV_ACTIVATED 靶面环数段 + 灯臂/侧灯臂两条**整条常亮定值** (老 state3: Buff_Activating;
 *                 1 环特殊 = 1/2/4/6/8/10 六段叠加, 老 main.c 原样;
 *                 常亮与 [6]/round 无任何关联 —— 复刻口径, n/5 归 S8)
 *   SLV_ALL_ON    上/下两条整条定值    (老 state4; 外观待与规则图 5-23 对照 → S7 组 4)
 *
 * 亮度(§3.5 亮度换算, 差异 #14; 2026-10-10 验收修正): 老函数**不做阈值化**(那只在从未被
 * 调用的死代码里), 入参直接写灯珠 → 老调用点两档: `Buff_Activatable(255,·)` 靶面 19 段、
 * `Buff_Activating` 内部写死 255(环数段) = 满亮度; `Buff_Arrowhead_Flowing(&t,10,·)` 箭头、
 * state3 灯臂常亮、state4 全激活 = **定值 10**(老 LSB-first 实际观感≈80, 位序陷阱三处)。
 * 新驱动 MSB-first → 三处"10"全部走 `SLV_LEVEL_ARROW/PROGRESS/ALL_ON`(config 一处改),
 * 10/80 观感定值归 S7 组 4 台架拍板。
 *
 * 动画节拍(S6.1): 老代码靠主循环≈200ms + 补丁变量 i/test_num 喂相位, 每 4 圈步进(≈800ms);
 * 新实现 = 4Hz 逻辑帧计数 + 模块内自持相位(LIGHT_ARROW_STEP_TICKS=4 → ≈1s/步),
 * 不再有外部补丁变量。清屏只发生在 light_set_scene()(状态转换), 每拍重画为幂等覆盖。
 */
#include "light.h"
#include "bsp_ws2812.h"
#include "bsp_switch.h"
#include "config_light.h"
#include "config_ws2812.h"
#include "config_board.h"

typedef struct
{
    uint16_t start;
    uint16_t len;
} light_seg_t;

#define SEG_NUM(a)  (sizeof(a) / sizeof((a)[0]))

static const light_seg_t s_armed_segs[] = LIGHT_ARMED_SEGS;     /* 老 Buff_Activatable: 19 段 */
static const light_seg_t s_ring_segs[]  = LIGHT_RING_MAP;       /* 老 Buff_Activating: 下标 = 环数-1 */
static const light_seg_t s_arrow_rows[] = LIGHT_ARROWHEAD_ROWS; /* 老 Buff_Arrowhead: 6 行模板 */

/* 老 Flowing: 偏移 0,4,...,40 共 11 份箭头叠加(别数错次数 —— 老主机版是 9 份, 那不是真相源) */
#define ARROW_COPIES   11u
#define ARROW_PHASES    4u   /* 相位 0..3(老 count++ 满 4 归 0) */

static slv_state_t s_scene = SLV_SLEEP;
static uint8_t     s_circle;        /* ACTIVATED 的环数(1~10; 0=异常, 不画段) */
static bool        s_blue;          /* 帧 [4]: 0=红 1=蓝 */
static uint8_t     s_phase;         /* 箭头相位 0..3(老 static count) */
static uint8_t     s_phase_div;     /* 每 4 逻辑帧步进一次(老 i 0..3) */

/* ── 调试快照(长期保留, Watch g_dbg_pat; S7 组 4 主判据) ───────────── */
volatile struct
{
    uint8_t scene;        /* slv_state_t */
    uint8_t blue;
    uint8_t circle;
    uint8_t phase;        /* 箭头相位(应每 4 帧 +1, 4 归 0) */
    uint8_t phase_div;
    uint8_t test;         /* 1=白光自检进行中 */
} g_dbg_pat;

/* ── 白光逐条自检(《从机灯带自检与排障》; 本板未接拨码 → 只能 DEBUG_TEST_MODE 宏触发) ──
 * 变量名与主机工程一致(g_dbg_ws_scan_idx), 排障文档可直接引用。 */
volatile uint8_t g_dbg_ws_scan_idx = 0xFF;
static bool     s_test_mode;
static uint8_t  s_active[WS2812_STRIP_NUM];   /* 本机实接灯带(led_num>0)索引表 */
static uint8_t  s_active_cnt;
static uint8_t  s_test_pos;
static uint16_t s_test_cnt;

static bool test_triggered(void)
{
#if DEBUG_TEST_MODE
    return true;
#elif TEST_TRIGGER_USE_SW2
    return (bsp_switch_get().sw_run != 0u);   /* ⚠ 从机板未接拨码, 此路禁用(宏保持 0) */
#else
    return false;
#endif
}

/* ── 小工具 ───────────────────────────────────────────────── */

static void scene_clear_all(void)         /* 5 条主动灯带全灭(SPARE 灯数 0, fill 自然跳过) */
{
    for (uint8_t i = 0u; i < WS2812_STRIP_NUM; i++)
    {
        bsp_ws2812_fill(i, 0u, 0u, 0u);
    }
}

static void scene_color(uint8_t *r, uint8_t *b)
{
    *r = s_blue ? 0u : 255u;              /* 图案满亮度(老 Buff_* 阈值入参的等效值) */
    *b = s_blue ? 255u : 0u;
}

/* 老 Buff_Activating: 按环数画一段(红蓝几何完全相同, 只差通道 → 一张表 + 颜色) */
static void ring_show(uint8_t circle)
{
    uint8_t r, b;

    if (circle < 1u || circle > 10u)
    {
        return;
    }
    scene_color(&r, &b);
    bsp_ws2812_set_rgb(SLV_STRIP_TARGET, s_ring_segs[circle - 1u].start,
                       s_ring_segs[circle - 1u].len, r, 0u, b);
}

/* ── 四个场景的绘制(每 4Hz 一拍, 幂等重画 = 老代码"每圈重画"的等效) ── */

static void draw_armed(void)
{
    uint8_t r, b;
    uint8_t ar, ab;
    uint8_t i, k;

    scene_color(&r, &b);                            /* 255 档: 靶面 19 段 */
    ar = s_blue ? 0u : (uint8_t)SLV_LEVEL_ARROW;    /* 10 档: 箭头(老调用实参, 见文件头) */
    ab = s_blue ? (uint8_t)SLV_LEVEL_ARROW : 0u;

    /* 靶面 19 段(老 Buff_Activatable: 整条清掉再逐段画) */
    bsp_ws2812_fill(SLV_STRIP_TARGET, 0u, 0u, 0u);
    for (i = 0u; i < (uint8_t)SEG_NUM(s_armed_segs); i++)
    {
        bsp_ws2812_set_rgb(SLV_STRIP_TARGET, s_armed_segs[i].start,
                           s_armed_segs[i].len, r, 0u, b);
    }

    /* 灯臂箭头(老 Buff_Arrowhead_Flowing: 整条清掉 → 11 份偏移 k*4-phase 叠加,
       颜色 = 老调用原值 10。老越界守卫只查起始索引; 新驱动统一校验 start+count
       (差异 #16 有意加强) → 贴边 1 颗的段整体不写, 观感等价且不再写坏相邻缓冲 */
    bsp_ws2812_fill(SLV_STRIP_ARM, 0u, 0u, 0u);
    for (k = 0u; k < ARROW_COPIES; k++)
    {
        int32_t off = (int32_t)(k * 4u) - (int32_t)s_phase;

        for (i = 0u; i < (uint8_t)SEG_NUM(s_arrow_rows); i++)
        {
            int32_t start = (int32_t)s_arrow_rows[i].start + off * 8;

            if (start >= 0 && start < (int32_t)WS_LED_NUM_ARM)
            {
                bsp_ws2812_set_rgb(SLV_STRIP_ARM, (uint16_t)start,
                                   s_arrow_rows[i].len, ar, 0u, ab);
            }
        }
    }

    /* 相位: 每 LIGHT_ARROW_STEP_TICKS 帧步进一次(老 i==4 → ≈800ms~1s/步) */
    if (++s_phase_div >= LIGHT_ARROW_STEP_TICKS)
    {
        s_phase_div = 0u;
        s_phase++;
        if (s_phase >= ARROW_PHASES)
        {
            s_phase = 0u;
        }
    }
}

static void draw_activated(void)
{
    uint8_t lvl;

    /* 靶面环数段: 1 环特殊 = 1/2/4/6/8/10 六段叠加(老 main.c state3 原样) */
    bsp_ws2812_fill(SLV_STRIP_TARGET, 0u, 0u, 0u);
    if (s_circle == 1u)
    {
        static const uint8_t c1[6] = {1u, 2u, 4u, 6u, 8u, 10u};

        for (uint8_t j = 0u; j < 6u; j++)
        {
            ring_show(c1[j]);
        }
    }
    else
    {
        ring_show(s_circle);
    }

    /* 灯臂 + 侧灯臂: 两条整条常亮, 定值与 [6]/round 无关(复刻老行为, n/5 归 S8)。
       老代码红=(10,0,0) 蓝=(0,0,10) —— 值走 SLV_LEVEL_*(位序陷阱见文件头) */
    lvl = (uint8_t)SLV_LEVEL_PROGRESS;
    if (s_blue)
    {
        bsp_ws2812_fill(SLV_STRIP_ARM,     0u, 0u, lvl);
        bsp_ws2812_fill(SLV_STRIP_ARM_SIDE, 0u, 0u, lvl);
    }
    else
    {
        bsp_ws2812_fill(SLV_STRIP_ARM,     lvl, 0u, 0u);
        bsp_ws2812_fill(SLV_STRIP_ARM_SIDE, lvl, 0u, 0u);
    }
}

static void draw_all_on(void)
{
    uint8_t lvl = (uint8_t)SLV_LEVEL_ALL_ON;
    uint8_t r   = s_blue ? 0u : lvl;
    uint8_t b   = s_blue ? lvl : 0u;

    /* 老 state4: 只画 上/下 两条(其余条由入口清屏保持熄灭, 每拍不重复写) */
    for (uint8_t i = 0u; i < WS2812_STRIP_NUM; i++)
    {
        if (SLV_ALL_ON_STRIP_MASK & (1u << i))
        {
            bsp_ws2812_fill(i, r, 0u, b);
        }
    }
}

/* ── 对外接口 ─────────────────────────────────────────────── */

void light_init(void)
{
    s_scene = SLV_SLEEP;
    s_circle = 0u;
    s_blue = false;
    s_phase = 0u;
    s_phase_div = 0u;
    s_test_mode = false;
}

void light_set_scene(slv_state_t scene, uint8_t circle)
{
    s_scene = scene;
    if (scene == SLV_ACTIVATED)
    {
        s_circle = circle;
    }
    else if (scene != SLV_ARMED)
    {
        s_circle = 0u;
    }
    scene_clear_all();          /* 全工程唯一清屏点(差异表 #12: 每次转换统一清) */
    g_dbg_pat.scene = (uint8_t)scene;
    g_dbg_pat.circle = s_circle;
}

void light_set_color(bool is_blue)
{
    s_blue = is_blue;
    g_dbg_pat.blue = is_blue ? 1u : 0u;
}

void light_tick(void)
{
    /* ── 白光逐条自检(排障工具, 上电即扫的是调试固件) ── */
    if (test_triggered())
    {
        if (!s_test_mode)
        {
            s_active_cnt = 0u;
            for (uint8_t i = 0u; i < WS2812_STRIP_NUM; i++)
            {
                if (bsp_ws2812_led_num(i) > 0u)
                {
                    s_active[s_active_cnt++] = i;
                }
            }
            s_test_mode = true;
            s_test_pos  = 0u;
            s_test_cnt  = 0u;
        }

        if (s_active_cnt > 0u)
        {
            for (uint8_t i = 0u; i < s_active_cnt; i++)
            {
                bsp_ws2812_fill(s_active[i], 0u, 0u, 0u);
            }
            bsp_ws2812_fill(s_active[s_test_pos], 255u, 255u, 255u);
            g_dbg_ws_scan_idx = s_active[s_test_pos];

            if (++s_test_cnt >= LIGHT_TEST_TICKS)
            {
                s_test_cnt = 0u;
                s_test_pos = (uint8_t)((s_test_pos + 1u) % s_active_cnt);
            }
        }
        g_dbg_pat.test = 1u;
        return;                 /* 自检期间盖掉场景 */
    }

    if (s_test_mode)            /* 退出自检: 清掉白扫描, 恢复当前场景 */
    {
        s_test_mode = false;
        g_dbg_ws_scan_idx = 0xFF;
        g_dbg_pat.test = 0u;
        scene_clear_all();
    }

    switch (s_scene)
    {
    case SLV_SLEEP:             /* 老 state1: 每圈清 5 条(持续压住任何残留) */
        scene_clear_all();
        break;
    case SLV_ARMED:
        draw_armed();
        break;
    case SLV_ACTIVATED:
        draw_activated();
        break;
    case SLV_ALL_ON:
        draw_all_on();
        break;
    default:
        break;
    }

    g_dbg_pat.scene     = (uint8_t)s_scene;
    g_dbg_pat.circle    = s_circle;
    g_dbg_pat.phase     = s_phase;
    g_dbg_pat.phase_div = s_phase_div;
}
