/**
 * @file bsp_switch.c
 * @brief 拨码(3个)与板卡 ID(3位)读取。引脚与标签由 CubeMX 生成(main.h)
 *
 * ⚠ 原理图上的 SW4(PB5, "调试模式") 实物未装配, 旧代码也只定义了 SW[3];
 *   PB5 在 CubeMX 中保留为 GPIO 输入(内部上拉)但**本模块不读取**,
 *   下一版装上开关即可启用。
 *
 * 消抖: 4Hz 逻辑帧采样, 候选值连续 SWITCH_DEBOUNCE_TICKS 次一致才更新稳定值。
 * 极性: 引脚电平按 config_board.h 的 ACTIVE_LEVEL 归一成"激活"位。
 * ⚠ 极性宏是旧代码假设, 上板万用表核对后回填(P4.5)。
 */
#include "bsp_switch.h"
#include "main.h"
#include "config_board.h"

#define SW_NUM  3u

typedef struct
{
    uint8_t cand;   /* 候选激活位 */
    uint8_t cnt;    /* 连续一致计数 */
} sw_db_t;

static switch_state_t s_state;
static sw_db_t s_db[SW_NUM];

/* 引脚表: 顺序对应 sw_buff / sw_run / sw_color */
static GPIO_TypeDef *const s_ports[SW_NUM] = {
    SW_BUFF_GPIO_Port, SW_RUN_GPIO_Port, SW_COLOR_GPIO_Port
};
static const uint16_t s_pins[SW_NUM] = {
    SW_BUFF_Pin, SW_RUN_Pin, SW_COLOR_Pin
};
static const uint8_t s_active[SW_NUM] = {
    SW_BUFF_ACTIVE_LEVEL, SW_RUN_ACTIVE_LEVEL, SW_COLOR_ACTIVE_LEVEL
};

/* 单路消抖: 返回 0xFF=尚未稳定(保持现值), 否则返回新稳定值 */
static uint8_t debounce_one(sw_db_t *db, uint8_t effective)
{
    if (effective == db->cand)
    {
        if (db->cnt < SWITCH_DEBOUNCE_TICKS) db->cnt++;
        if (db->cnt >= SWITCH_DEBOUNCE_TICKS)
        {
            return db->cand;
        }
    }
    else
    {
        db->cand = effective;
        db->cnt  = 0;
    }
    return 0xFF;
}

void bsp_switch_init(void)
{
    for (uint8_t i = 0; i < SW_NUM; i++)
    {
        uint8_t lvl = (HAL_GPIO_ReadPin(s_ports[i], s_pins[i]) == GPIO_PIN_SET) ? 1u : 0u;
        s_db[i].cand = (lvl == s_active[i]) ? 1u : 0u;   /* 按极性归一 */
        s_db[i].cnt  = SWITCH_DEBOUNCE_TICKS;            /* 上电立即视为稳定 */
    }

    s_state.sw_buff  = s_db[0].cand;
    s_state.sw_run   = s_db[1].cand;
    s_state.sw_color = s_db[2].cand;
}

void bsp_switch_poll(void)
{
    for (uint8_t i = 0; i < SW_NUM; i++)
    {
        uint8_t lvl = (HAL_GPIO_ReadPin(s_ports[i], s_pins[i]) == GPIO_PIN_SET) ? 1u : 0u;
        uint8_t eff = (lvl == s_active[i]) ? 1u : 0u;
        uint8_t r   = debounce_one(&s_db[i], eff);

        if (r != 0xFF)
        {
            switch (i)
            {
            case 0: s_state.sw_buff  = r; break;
            case 1: s_state.sw_run   = r; break;
            case 2: s_state.sw_color = r; break;
            default: break;
            }
        }
    }
}

switch_state_t bsp_switch_get(void)
{
    return s_state;
}

uint8_t bsp_switch_board_id(void)
{
    /* 外部 10kΩ 上拉, 三档拨动开关把其中一路拉到 GND */
    if (HAL_GPIO_ReadPin(BOARD_ID1_GPIO_Port, BOARD_ID1_Pin) == GPIO_PIN_RESET) return 0;
    if (HAL_GPIO_ReadPin(BOARD_ID2_GPIO_Port, BOARD_ID2_Pin) == GPIO_PIN_RESET) return 1;
    if (HAL_GPIO_ReadPin(BOARD_ID3_GPIO_Port, BOARD_ID3_Pin) == GPIO_PIN_RESET) return 2;
    return 0xFF;    /* 无挡位/悬空 */
}
