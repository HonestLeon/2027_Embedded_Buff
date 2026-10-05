/**
 * @file bsp_rng.c
 * @brief 上电取一次随机种子。替代老代码三套混用种子
 *        (srand(1) / srand(CNT_my) / TIM2中断 random_seed+=7)。
 */
#include "bsp_rng.h"
#include "rng.h"
#include "main.h"

uint32_t bsp_rng_get_seed(void)
{
    uint32_t seed = 0;

    if (HAL_RNG_GenerateRandomNumber(&hrng, &seed) == HAL_OK)
    {
        return seed;
    }

    /* RNG 异常兜底: DWT 周期计数 ^ SysTick 毫秒计数, 熵有限但可启动 */
    return (uint32_t)(DWT->CYCCNT ^ HAL_GetTick());
}
