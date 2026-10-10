/**
 * @file hit_detect.h
 * @brief 击打采集与环数判定(S4) —— 仅从机构建
 *
 * 分工(《从机代码重构计划》S4 / 《从机重构清单》§3.4):
 *   EXTI 中断只做"记录"(引脚+时间戳入环形缓冲); 判定/查表/事件全在 hit_detect_task()。
 *   事件由从机状态机(S5)在 4Hz 里 take 消费: HIT → ACTIVATED+上报, MISHIT → SLEEP+打错上报。
 */
#ifndef HIT_DETECT_H
#define HIT_DETECT_H

#include <stdbool.h>
#include <stdint.h>

/* 击打事件(单槽, take 读后清空) */
typedef enum
{
    HD_EV_NONE = 0,
    HD_EV_HIT,       /* ARMED 状态有效击发: circle = 环数(1~10) */
    HD_EV_MISHIT,    /* 打错: 非 ARMED 被击(休眠/已激活/全激活) → 上层上报复位 */
} hd_event_t;

/* 状态快照(仅观测用, 语义对齐老代码 once 三态 + start 锁存):
 *   ARMED  = 可击发(老 once==0 && state==2)
 *   FIRED  = 已击发, 事件尚未被状态机消费(打同一引脚的后续沿按时间去抖忽略)
 *   LOCKED = 未武装(任何引脚被击都是打错候选)
 *   MISHIT = 有待消费的打错事件 */
typedef enum
{
    HD_ST_LOCKED = 0,
    HD_ST_ARMED,
    HD_ST_FIRED,
    HD_ST_MISHIT,
} hd_st_t;

void       hit_detect_init(void);
void       hit_detect_set_armed(bool armed);   /* 由从机状态机驱动: 仅 SLV_ARMED 为 true;
                                                  变 true 即重新武装(清击发锁存 ≈ 老代码休眠时 once=0) */
void       hit_detect_task(void);              /* 4Hz: 排空记录环 → 查表判定 → 置事件 */
hd_event_t hit_detect_take(uint8_t *circle, uint16_t *pin);  /* 取走并清空待消费事件 */

#endif /* HIT_DETECT_H */
