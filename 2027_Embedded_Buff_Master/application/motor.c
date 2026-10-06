/**
 * @file motor.c
 * @brief 电机控制(P5 完整实现) —— 算法与参数迁移自 2025 老主机, 逐行对照见《重构对照》第7节
 *
 * 控制结构(老代码原样):
 *   目标位置(电机圈) ──[位置环 P=200]──► 速度给定(rpm) ──[速度环 P=15]──► 电流 + 前馈
 *   目标位置由"角速度给定 v(rad/s)"积分而来: target += CIRCLE_NUM/(2π/v) * dt
 *   (CIRCLE_NUM=153.5 = 大符转一圈对应的电机圈数; 位置 = 过零圈数 + 角度/360)
 *   大符: v = A·sin(w·t)+B, 并叠加前馈 FF_K·cos(w·t+φ) 到电流输出
 *   小符: v = π/3 匀速, 无前馈
 *   SW_RUN=停 时: 目标跟随当前位置 + 相位清零 + 电流输出 0(老代码行为)
 *
 * 说明:
 *   - 三角函数用标准 sinf/cosf(老代码用 CMSIS-DSP 的 arm_sin_f32/arm_cos_f32,
 *     但新 Keil 工程未链接 DSP 库; 1kHz 调两次, 性能差异可忽略);
 *   - dt 由 TIM6 节拍给定(恒定 0.001), 不再需要老代码的 DWT 实测补偿;
 *   - 0x200 每 1ms 发一次(含停转时的 0 电流), 这是电调的保活帧。
 */
#include "motor.h"
#include "config_motor.h"
#include "config_can.h"
#include "bsp_can.h"
#include <math.h>

/* ── 老代码结构体(逐字段对应) ───────────────────────────── */
typedef struct
{
    float set_point;                /* 设定目标值   */
    float p, i, d;                  /* 三个系数     */
    float last_error, pre_error;    /* 前次/当前误差 */
    float sum_error, d_error;       /* 积分/微分项   */
    float i_max;                    /* 积分限幅     */
} pid_t;

typedef struct
{
    float position;                 /* 电机位置(圈)  */
    float target;                   /* 目标位置(圈)  */
    float delat;                    /* 位置误差      */
} motor_ctrl_t;

typedef struct
{
    float circle;                   /* 转过圈数      */
    float last_value;               /* 上次检测值(度) */
    float actual_value;             /* 当前检测值(度) */
} zero_check_t;

typedef struct
{
    int16_t angle;                  /* 0~8191 机械角  */
    int16_t real_speed;             /* rpm           */
    int16_t current;                /* 原始电流值     */
} motor_fb_t;

/* ── 内部状态 ───────────────────────────────────────────── */
static pid_t         s_pid_pos;
static pid_t         s_pid_spd;
static motor_ctrl_t  s_ctrl;
static zero_check_t  s_zero;
static motor_fb_t    s_fb[MOTOR_NUM];

volatile struct
{
    int16_t  fb_angle;
    int16_t  fb_speed;
    float    target;
    float    position;
    float    delat;
    float    cmd_current;
    uint32_t fb_frames;
} g_dbg_motor;

static bool          s_run;         /* SW_RUN: 转=1 */
static bool          s_big;         /* SW_BUFF: 大符=1 */
static float         s_speed_t;     /* 正弦相位积分(s), 停转时清零 */
static uint16_t      s_tx_div;      /* 0x200 发帧分频(见 MOTOR_TX_PERIOD_MS) */
static uint32_t      s_fb_frames;   /* 累计收到的电机反馈帧数 */

/* ── 电机环调试快照(长期保留, ~30 字节) ─────────────────────
 * 调试器 Watch `g_dbg_motor` 一眼看清闭环状态:
 *   fb_frames : 是否在增长 → 电调反馈有没有进来(速度环是否真的闭环!)
 *   fb_speed  : 反馈转速(rpm);  fb_angle: 反馈角度(0~8191)
 *   target    : 目标位置(电机圈), 小符应约 25.58 圈/秒; position: 实际位置
 *   delat     : 位置误差;      cmd_current: 本次发出的电流指令 */

/* ── 限幅(老代码 LIMIT_MAX_MIN 宏) ──────────────────────── */
static float clamp_f(float x, float max, float min)
{
    if (x > max) return max;
    if (x < min) return min;
    return x;
}

/* ── PID(老 PID_Calc 原样: 输出限幅 ±10000) ─────────────── */
static float pid_calc(pid_t *p, float actual_value)
{
    p->pre_error  = p->set_point - actual_value;
    p->d_error    = p->pre_error - p->last_error;
    p->sum_error += p->pre_error;
    p->last_error = p->pre_error;

    if (p->sum_error >= p->i_max)
    {
        p->sum_error = p->i_max;
    }
    else if (p->sum_error <= -p->i_max)
    {
        p->sum_error = -p->i_max;
    }

    return clamp_f(p->p * p->pre_error + p->i * p->sum_error + p->d * p->d_error,
                   10000.0f, -10000.0f);
}

/* ── 过零检测(老 Zero_Check 原样) ───────────────────────── */
static void zero_check_update(zero_check_t *z)
{
    if (z->actual_value - z->last_value > 180.0f)
    {
        z->circle -= 1.0f;
    }
    else if (z->actual_value - z->last_value < -180.0f)
    {
        z->circle += 1.0f;
    }
    z->last_value = z->actual_value;
}

/* ── 对外接口 ───────────────────────────────────────────── */

void motor_init(void)
{
    s_pid_pos.p = MOTOR_POS_KP;
    s_pid_spd.p = MOTOR_SPD_KP;
    /* I/D 保持 0(老代码只设了 P, IMax=0 → 积分项恒为 0, 即纯 P 双环) */
    s_run     = false;
    s_big     = false;
    s_speed_t = 0.0f;
}

void motor_set_run(bool run)
{
    s_run = run;
}

void motor_set_mode(bool big)
{
    s_big = big;
}

void motor_on_feedback(uint32_t id, const uint8_t raw[8])
{
    uint8_t idx;

    if (id < CAN_ID_MOTOR_FB_BASE || id >= (CAN_ID_MOTOR_FB_BASE + MOTOR_NUM))
    {
        return;                             /* 非电机反馈帧 */
    }
    idx = (uint8_t)(id - CAN_ID_MOTOR_FB_BASE);
    s_fb_frames++;

    s_fb[idx].angle      = (int16_t)((raw[0] << 8) | raw[1]);
    s_fb[idx].real_speed = (int16_t)((raw[2] << 8) | raw[3]);
    s_fb[idx].current    = (int16_t)((raw[4] << 8) | raw[5]);
}

void motor_update(float dt)
{
    uint32_t id;
    uint8_t  rx[8];
    float    v;                 /* 角速度给定 rad/s   */
    float    speed;             /* 位置增量速率 圈/s  */
    float    ff = 0.0f;         /* 前馈电流           */
    float    cur;

    /* ① 排空 CAN2 反馈队列(1kHz 级, 必须每个 1ms 消费, 不能等到 4Hz 逻辑帧) */
    while (bsp_can2_pop(&id, rx))
    {
        motor_on_feedback(id, rx);
    }

    /* ② 速度给定: 小符匀速 / 大符正弦(老公式原样) */
    if (s_big)
    {
        float phase = MOTOR_BIG_W * s_speed_t;
        v  = MOTOR_BIG_A * sinf(phase) + MOTOR_BIG_B;
        ff = MOTOR_FF_K * cosf(phase + MOTOR_FF_PHASE);
    }
    else
    {
        v = MOTOR_SMALL_SPEED;
    }
    speed = MOTOR_CIRCLE_NUM / (2.0f * 3.1415926f / v);

    /* ③ 目标位置积分 / 停转时跟随当前位置(老 test_flag 分支) */
    if (s_run)
    {
        s_ctrl.target += speed * dt;
        s_speed_t     += dt;
    }
    else
    {
        s_ctrl.target = s_ctrl.position;
        s_speed_t     = 0.0f;
    }

    /* ④ 位置反馈: 编码器角度 → 度 → 过零圈数 + 角度/360 = 电机圈数 */
    s_zero.actual_value = 360.0f * (float)s_fb[0].angle / (float)MOTOR_ENC_CPR;
    zero_check_update(&s_zero);
    s_ctrl.position = s_zero.circle + s_zero.actual_value / 360.0f;
    s_ctrl.delat    = s_ctrl.target - s_ctrl.position;

    /* ⑤ 双环级联(老代码: 位置环输出取负作为速度环给定) */
    s_pid_pos.set_point = 0.0f;
    s_pid_spd.set_point = -pid_calc(&s_pid_pos, s_ctrl.delat);
    cur = pid_calc(&s_pid_spd, (float)s_fb[0].real_speed);

    /* ⑥ 大符叠加前馈; 停转强制 0(老代码行为) */
    if (s_big)
    {
        cur += ff;
    }
    if (!s_run)
    {
        cur = 0.0f;
    }
    cur = clamp_f(cur, (float)MOTOR_CUR_LIMIT, -(float)MOTOR_CUR_LIMIT);

    /* ⑦ 调试快照(仅观测, 不影响控制) */
    g_dbg_motor.fb_angle    = s_fb[0].angle;
    g_dbg_motor.fb_speed    = s_fb[0].real_speed;
    g_dbg_motor.target      = s_ctrl.target;
    g_dbg_motor.position    = s_ctrl.position;
    g_dbg_motor.delat       = s_ctrl.delat;
    g_dbg_motor.cmd_current = cur;
    g_dbg_motor.fb_frames   = s_fb_frames;

    /* ⑧ 发 0x200 电流帧: 槽位 1 = 本电机, 其余 0(老 CAN_Motor_Send1 行为)
       发送周期由 MOTOR_TX_PERIOD_MS 控制(控制环仍是 1ms) */
    if (++s_tx_div < MOTOR_TX_PERIOD_MS)
    {
        return;
    }
    s_tx_div = 0u;
    {
        int16_t c = (int16_t)cur;
        uint8_t tx[8];

        tx[0] = (uint8_t)((uint16_t)c >> 8);
        tx[1] = (uint8_t)((uint16_t)c & 0xFFu);
        tx[2] = 0u; tx[3] = 0u;
        tx[4] = 0u; tx[5] = 0u;
        tx[6] = 0u; tx[7] = 0u;
        (void)bsp_can2_send(CAN_ID_MOTOR_TX, tx);
    }
}
