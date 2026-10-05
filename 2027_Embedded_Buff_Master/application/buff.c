/**
 * @file buff.c
 * @brief P0 骨架: 协议/状态机/随机 待 P4(pack/parse)+P6(状态机+随机)。
 *
 * 随机生成规则(老代码原样): 小符5个不重复; 大符前后轮可重复、同轮b[i]≠a[i]。
 * ⚠ 老函数内部的 srand(CNT_my)/srand(random_seed) 必须移除——CNT_my 无人递增、
 *   TIM2 已删; 种子统一由上电 bsp_rng_get_seed 注入, 函数只做纯生成。
 */
#include "buff.h"
#include "config_buff.h"

uint8_t buff_frame_pack(uint8_t out[8], const buff_frame_t *f)
{
    (void)f;
    /* TODO P4.4: [0]=0xaa [1]=cmd [2]=大小符 [3]=panel_a [4]=红蓝 [5]=panel_b [6]=round */
    (void)out;
    return 8;
}

bool buff_frame_parse(const uint8_t in[8], buff_frame_t *f)
{
    (void)in; (void)f;
    /* TODO P4.4: 校验包头0xaa + 逐字段解包 */
    return false;
}

void buff_init(void)
{
    /* TODO P6.1: 取随机序列(去srand化), 清计数 */
}

void buff_step(void)
{
    /* TODO P6.3: 排空 bsp_can1_pop(灯板回报/复位) + 拨码变化 → 状态转换/发帧 */
}

void buff_tick(float dt)
{
    (void)dt;
    /* TODO P6.2: 2.5s超时 / 1s窗口 计时 */
}
