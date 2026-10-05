/**
 * @file light.h
 * @brief 主机板载灯带灯效(开机红蓝、中心圆环等本地图案)
 */
#ifndef LIGHT_H
#define LIGHT_H

#include <stdbool.h>

void light_init(void);              /* 上电灯效: 按拨码红蓝全点亮(老main.c原行为) */
void light_set_color(bool is_blue); /* 拨码变化时刷新红蓝(修老"只刷一次"bug)      */
void light_tick(void);              /* 4Hz: 本地灯效动画(后续扩展)                */

#endif /* LIGHT_H */
