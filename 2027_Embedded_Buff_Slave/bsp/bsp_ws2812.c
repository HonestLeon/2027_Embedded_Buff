/**
 * @file bsp_ws2812.c
 * @brief WS2812 六路驱动(P2 完整实现)
 *
 * 原理: PWM 周期=1bit(1.25µs@800kHz, ARR=34/PSC=2 永不改动), DMA 把
 *       ccr_buf 里的占空比序列逐个灌进各通道 CCR(每通道独立, 互不影响);
 *       帧尾 90 个 0 构成复位段(112µs > 50µs 规格)。
 *
 * 握手: 图案层写 rgb 缓冲置 dirty → refresh_all 对 "!busy && dirty" 的灯带
 *       translate+Start_DMA+busy=1 → DMA TC 中断(PulseFinishedCallback)清 busy。
 *       ⚠ 无需 Stop_DMA: HAL 在 Normal 模式下于回调前已把通道状态复位为 READY
 *       (已核对本工程 hal_tim.c 的 TIM_DMADelayPulseCplt); DMABurstState 与
 *       PWM Start_DMA 路径无关。
 *
 * 吞吐: 每 tick 最多启动 2 条(ISR 耗时保护); 最长一帧 320 灯≈9.7ms,
 *       6 条依次启动全部刷完约 12ms << 图案周期 250ms。
 * 与主机版的差异: ① 下面是 6 对缓冲(主机 1 对), 容量全部由 config 的 WS_LED_NUM_* 推导;
 *                ② 用 s_desc 描述表展开(消除 -Wall 的缺省初始化警告)。驱动逻辑完全相同。
 */
#include "bsp_ws2812.h"
#include "tim.h"
#include "config_ws2812.h"
#include <string.h>

/* ── 静态缓冲(主 SRAM! DMA 不能访问 CCM) ──────────────────────
 * 索引与 config_ws2812.h 的 WS2812_STRIP_TBL 一一对应。灯数只在 config 里用
 * WS_LED_NUM_* 定义一次, 容量由它推导 → 不存在"改了灯带表忘了改缓冲"的越界。
 * 灯数为 0 的通道仍留 1 颗占位(避免零长数组), init/refresh 会跳过它。
 *
 * RAM(老口径 881 颗): Σ(灯×24+90)×2B + Σ(灯×3) ≈ 45KB。
 *   Keil 按 Target 对话框生成的 scatter 只有 RW_IRAM1(0x20000000,112KB) +
 *   RW_IRAM2(0x2001C000,16KB), **CCM(0x10000000) 不在数据区** → 静态缓冲不可能
 *   落到 DMA 访问不到的 CCM(此结论取自 2026 主机工程的 .sct)。 */
#define WS_DUMMY_LED      1u                                /* 灯数=0 的通道占位 */
#define WS_CCR_LEN(led)   (((led) ? (led) : WS_DUMMY_LED) * 24u + WS2812_RESET_BITS)
#define WS_RGB_LEN(led)   (((led) ? (led) : WS_DUMMY_LED) * 3u)

static uint16_t s_ccr0[WS_CCR_LEN(WS_LED_NUM_UP)];          static uint8_t s_rgb0[WS_RGB_LEN(WS_LED_NUM_UP)];
static uint16_t s_ccr1[WS_CCR_LEN(WS_LED_NUM_TARGET)];      static uint8_t s_rgb1[WS_RGB_LEN(WS_LED_NUM_TARGET)];
static uint16_t s_ccr2[WS_CCR_LEN(WS_LED_NUM_ARM)];         static uint8_t s_rgb2[WS_RGB_LEN(WS_LED_NUM_ARM)];
static uint16_t s_ccr3[WS_CCR_LEN(WS_LED_NUM_ARM_SIDE)];    static uint8_t s_rgb3[WS_RGB_LEN(WS_LED_NUM_ARM_SIDE)];
static uint16_t s_ccr4[WS_CCR_LEN(WS_LED_NUM_DOWN)];        static uint8_t s_rgb4[WS_RGB_LEN(WS_LED_NUM_DOWN)];
static uint16_t s_ccr5[WS_CCR_LEN(WS_LED_NUM_SPARE)];       static uint8_t s_rgb5[WS_RGB_LEN(WS_LED_NUM_SPARE)];

static uint16_t *const s_ccr_tbl[WS2812_STRIP_NUM] =
    { s_ccr0, s_ccr1, s_ccr2, s_ccr3, s_ccr4, s_ccr5 };
static uint8_t *const s_rgb_tbl[WS2812_STRIP_NUM] =
    { s_rgb0, s_rgb1, s_rgb2, s_rgb3, s_rgb4, s_rgb5 };

/* config 的表每行只给 {句柄, 通道, 灯数} 三项 → 先落成 const 描述表, 由 init 展开成
 * 运行期结构。直接用它初始化 s_strips 会让后面 5 个字段缺省, 触发
 * -Wmissing-field-initializers(DoD 要求 -Wall 无警告)。 */
typedef struct
{
    TIM_HandleTypeDef *tim;
    uint32_t           channel;
    uint16_t           led_num;
} ws2812_desc_t;

static const ws2812_desc_t s_desc[WS2812_STRIP_NUM] = WS2812_STRIP_TBL;
static ws2812_strip_t      s_strips[WS2812_STRIP_NUM];

/* "0"/"1" 码对应的 CCR 占空比计数值(老板实测时序) */
static const uint16_t s_bit_ccr[2] = { WS2812_T0H_CCR, WS2812_T1H_CCR };

/* ── 内部: RGB 逻辑缓冲 → CCR 序列(老 WS_2812_Set_RGB 算法原样) ── */
static void ws2812_translate(ws2812_strip_t *s)
{
    uint16_t *out = s->ccr_buf;
    const uint8_t *in = s->rgb;
    uint16_t led = s->led_num;

    while (led--)
    {
        const uint8_t grb[3] = { in[0], in[1], in[2] };   /* G,R,B 高位先行 */
        uint8_t byte_idx, bit;

        in += 3;
        for (byte_idx = 0; byte_idx < 3; byte_idx++)
        {
            for (bit = 0; bit < 8; bit++)
            {
                *out++ = s_bit_ccr[(grb[byte_idx] >> (7 - bit)) & 1u];
            }
        }
    }
    /* 复位段: 全 0 → 输出持续低电平 ≥112µs, 所有灯锁存 */
    for (uint16_t k = 0; k < WS2812_RESET_BITS; k++)
    {
        *out++ = 0;
    }
}

/* ── 对外接口 ─────────────────────────────────────────────── */

void bsp_ws2812_init(void)
{
    for (uint8_t i = 0; i < WS2812_STRIP_NUM; i++)
    {
        ws2812_strip_t *s = &s_strips[i];

        s->tim      = s_desc[i].tim;
        s->channel  = s_desc[i].channel;
        s->led_num  = s_desc[i].led_num;
        s->ccr_buf = s_ccr_tbl[i];
        s->rgb     = s_rgb_tbl[i];
        s->ch_idx  = (uint8_t)(1u << (s->channel >> 2));  /* 见文件头/配置清单 */
        s->dma_busy = 0;
        s->dirty    = 0;

        if (s->led_num > 0u)
        {
            /* 安全防护: led_num>0 但未分配缓冲(两处配置不同步)时跳过,
               否则 memset(NULL) 会硬件异常 */
            if (s->ccr_buf == NULL || s->rgb == NULL) continue;

            memset(s->ccr_buf, 0, WS_CCR_LEN(s->led_num) * sizeof(uint16_t));
            memset(s->rgb,    0, WS_RGB_LEN(s->led_num));
        }
    }
}

void bsp_ws2812_set_rgb(uint8_t strip, uint16_t start, uint16_t count,
                        uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_strip_t *s;

    if (strip >= WS2812_STRIP_NUM) return;
    s = &s_strips[strip];
    if (s->rgb == NULL || count == 0u) return;
    if ((uint32_t)start + count > s->led_num) return;

    for (uint16_t i = 0; i < count; i++)
    {
        uint8_t *p = &s->rgb[(start + i) * 3u];
        p[0] = g;   /* GRB 字节序 */
        p[1] = r;
        p[2] = b;
    }
    s->dirty = 1;
}

void bsp_ws2812_fill(uint8_t strip, uint8_t r, uint8_t g, uint8_t b)
{
    ws2812_strip_t *s;

    if (strip >= WS2812_STRIP_NUM) return;
    s = &s_strips[strip];
    bsp_ws2812_set_rgb(strip, 0, s->led_num, r, g, b);
}

uint16_t bsp_ws2812_led_num(uint8_t strip)
{
    if (strip >= WS2812_STRIP_NUM) return 0u;
    return s_strips[strip].led_num;
}

void bsp_ws2812_refresh_all(void)
{
    uint8_t started = 0;                    /* 每 tick 最多启动 2 条(ISR 保护) */

    for (uint8_t i = 0; i < WS2812_STRIP_NUM; i++)
    {
        ws2812_strip_t *s = &s_strips[i];

        if (started >= 2) break;
        if (s->led_num == 0u || s->dma_busy || !s->dirty) continue;

        ws2812_translate(s);
        s->dirty = 0;
        s->dma_busy = 1;

        if (HAL_TIM_PWM_Start_DMA(s->tim, s->channel, (uint32_t *)s->ccr_buf,
                                  (uint16_t)(s->led_num * 24u + WS2812_RESET_BITS)) != HAL_OK)
        {
            s->dma_busy = 0;
            s->dirty    = 1;                /* 恢复 dirty: 启动失败时别丢帧, 下个 tick 重试 */
        }
        else
        {
            started++;
        }
    }
}

/* ── DMA 传输完成: 帧结束的精确时刻 ─────────────────────────
 * 只清 busy。⚠ 不调 Stop_DMA: HAL(TIM_DMADelayPulseCplt)在 Normal 模式下
 * 于回调前已把通道状态复位为 READY, Start_DMA 可直接再次启动;
 * DMABurstState 属于 DMABurst_Read/Write API, 与 PWM Start_DMA 路径无关。
 * ⚠ 必须按 tim+Channel 双匹配: TIM3 上挂了 3 条灯带。 */
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    for (uint8_t i = 0; i < WS2812_STRIP_NUM; i++)
    {
        ws2812_strip_t *s = &s_strips[i];

        if (s->dma_busy && s->tim == htim && (uint32_t)s->ch_idx == htim->Channel)
        {
            s->dma_busy = 0;
        }
    }
}
