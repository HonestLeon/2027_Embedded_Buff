/**
 * @file bsp_rng.h
 * @brief 硬件随机数种子(F405 RNG 外设)
 */
#ifndef BSP_RNG_H
#define BSP_RNG_H

#include <stdint.h>

uint32_t bsp_rng_get_seed(void);

#endif /* BSP_RNG_H */
