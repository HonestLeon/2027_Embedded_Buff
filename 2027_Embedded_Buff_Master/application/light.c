/**
 * @file light.c
 * @brief 主机板载灯带灯效(P3 完整实现)
 *
 * 正常态: 按拨码红蓝全点亮本机灯带, 拨码切换即时生效(修复老 bug①: 只在上电刷一次)。
 * 测试态: 逐路白光测试 —— 遍历**本机实接的灯带**(led_num>0)依次白光
 *         LIGHT_TEST_TICKS×250ms, 其余灯带熄灭。
 *         ⚠ 实测主机板只接了"中心"一条(TIM3_CH1/PA6, 88颗), 故表现为该条持续白光;
 *         bsp_ws2812_led_num()=0 的通道自动跳过, 不会出现黑场空转。
 *         从机构建接入多条后, 本模式自动依次点亮各条。
 *
 * 测试触发方式(见 config_board.h):
 *   TEST_TRIGGER_USE_SW2=1 → 用 SW2(停/转) 触发
 *      ⚠ 仅在电机代码未落地(P5)期间使用! P5 落地后必须改为 0, 把 SW2 还给"停/转"。
 *   DEBUG_TEST_MODE=1       → 无条件进入测试(调试固件用)
 */
#include "light.h"
#include "bsp_ws2812.h"
#include "bsp_switch.h"
#include "config_light.h"
#include "config_ws2812.h"
#include "config_board.h"

static bool     s_is_blue;      /* 当前颜色状态(拨码红蓝)   */
static bool     s_test_mode;    /* 白光测试进行中            */
static uint8_t  s_active[WS2812_STRIP_NUM]; /* 本机实接的灯带索引表(测试用) */
static uint8_t  s_active_cnt;   /* 实接条数                  */
static uint8_t  s_test_pos;     /* 当前测试的是 s_active[] 的第几项 */
static uint16_t s_test_cnt;     /* 当前条已持续时间(4Hz tick)*/

/* 通道扫描诊断(长期保留, 1 字节): 2026 曾用它定位出"中心灯带=索引0";
   从机工程/改线后仍可用它快速定位, 或从此处快速查索引↔通道对照。
   当前白光扫描到的【灯带索引】(0~5), 0xFF = 未在扫描(正常灯色模式)。
   debugger 里 Watch 这个变量: 中心灯带亮起的那一刻它的值就是中心灯带所在索引。
   索引 ↔ 通道/引脚 对照:
     0 = TIM3_CH1 / PA6   (丝印 DIN6)  ← 主机唯一实接的"中心"灯带
     1 = TIM3_CH2 / PA7   (丝印 DIN5 外灯臂)
     2 = TIM3_CH4 / PB1   (丝印 DIN3 下灯带)
     3 = TIM4_CH1 / PB6   (丝印 DIN2 中心圆环)  ← 丝印最可疑
     4 = TIM4_CH2 / PB7   (丝印 DIN4 内灯臂)
     5 = TIM4_CH3 / PB8   (丝印 DIN1 上灯带) */
volatile uint8_t g_dbg_ws_scan_idx = 0xFF;

/* 白光测试是否应激活(触发方式由 config_board.h 决定) */
static bool test_triggered(void)
{
#if DEBUG_TEST_MODE
    return true;
#elif TEST_TRIGGER_USE_SW2
    return (bsp_switch_get().sw_run != 0u);   /* ⚠ 临时: SW2 兼作测试触发 */
#else
    return false;
#endif
}

/* 把"本机灯带"全刷成指定颜色(按 LIGHT_LOCAL_STRIP_MASK) */
static void fill_local(uint8_t r, uint8_t g, uint8_t b)
{
    for (uint8_t i = 0; i < WS2812_STRIP_NUM; i++)
    {
        if (LIGHT_LOCAL_STRIP_MASK & (1u << i))
        {
            bsp_ws2812_fill(i, r, g, b);
        }
    }
}

void light_init(void)
{
    switch_state_t sw = bsp_switch_get();   /* bsp_switch_init 已在之前调用 */

    s_is_blue = (sw.sw_color != 0);
    light_set_color(s_is_blue);
}

void light_set_color(bool is_blue)
{
    s_is_blue = is_blue;

    if (is_blue)
    {
        fill_local(0, 0, 255);
    }
    else
    {
        fill_local(255, 0, 0);
    }
}

void light_tick(void)
{
    /* 红蓝拨码切换 → 即时刷新(每帧都检查, 修复老"只刷一次"bug) */
    bool blue_now = (bsp_switch_get().sw_color != 0);
    if (blue_now != s_is_blue)
    {
        light_set_color(blue_now);
    }

    /* ── 逐路白光测试(仅遍历本机实接的灯带) ── */
    if (test_triggered())
    {
        if (!s_test_mode)
        {
            /* 进入测试: 建立"实接灯带"列表(led_num>0) */
            s_active_cnt = 0;
            for (uint8_t i = 0; i < WS2812_STRIP_NUM; i++)
            {
                if (bsp_ws2812_led_num(i) > 0u)
                {
                    s_active[s_active_cnt++] = i;
                }
            }
            s_test_mode = true;
            s_test_pos  = 0;
            s_test_cnt  = 0;
        }

        if (s_active_cnt > 0u)
        {
            /* 清掉所有实接灯带, 只亮当前测试条 */
            for (uint8_t i = 0; i < s_active_cnt; i++)
            {
                bsp_ws2812_fill(s_active[i], 0, 0, 0);
            }
            bsp_ws2812_fill(s_active[s_test_pos], 255, 255, 255);
            g_dbg_ws_scan_idx = s_active[s_test_pos];   /* ★诊断: 当前扫到的索引 */

            if (++s_test_cnt >= LIGHT_TEST_TICKS)
            {
                s_test_cnt = 0;
                s_test_pos = (uint8_t)((s_test_pos + 1u) % s_active_cnt);
            }
        }
    }
    else if (s_test_mode)
    {
        /* 退出测试: 恢复正常红蓝灯色 */
        s_test_mode = false;
        g_dbg_ws_scan_idx = 0xFF;       /* ★诊断: 退出扫描 */
        light_set_color(s_is_blue);
    }
}
