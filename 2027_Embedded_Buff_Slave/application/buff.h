/**
 * @file buff.h
 * @brief 从机灯板业务 —— CAN协议 + 大小符状态机 + 随机序列(单文件模块)
 *
 * 协议段为 2025 老语义, 主从共用: 从机工程复制本段后以 diff 校验同步。
 */
#ifndef BUFF_H
#define BUFF_H

#include <stdbool.h>
#include <stdint.h>

/* ── 协议 ── */
typedef enum
{
    BF_EXTINGUISH = 0x01,   /* 熄灭旧对     */
    BF_LIGHT      = 0x02,   /* 点亮         */
    BF_RESET      = 0x03,   /* 复位         */
    BF_ALL_ON     = 0x04,   /* 全亮         */
} bf_cmd_t;

typedef struct
{
    bf_cmd_t cmd;           /* 指令                          */
    uint8_t  is_big;        /* 1=大符                        */
    uint8_t  panel_a;       /* 灯板 1~5, 0=无                */
    uint8_t  panel_b;       /* 大符第二块 1~5, 0=无          */
    uint8_t  is_blue;       /* 0=红 1=蓝                     */
    uint8_t  round;         /* 大符第几次激活双灯板 0~5      */
} buff_frame_t;

uint8_t buff_frame_pack(uint8_t out[8], const buff_frame_t *f);   /* 恒返回8 */
bool   buff_frame_parse(const uint8_t in[8], buff_frame_t *f);   /* 校验包头0xaa */
bool   buff_send_frame(const buff_frame_t *f);                   /* 打包 + CAN1(0x161) 发送 */

/* ── 从机回报(从机 → 主机, 老协议: [1]==0x03 被击中 / [2]==0x01 打错复位) ── */
typedef enum
{
    SLAVE_RPT_HIT,      /* 被点亮/被击中, panel = 灯板号 1~5 */
    SLAVE_RPT_RESET,    /* 打错, 从机已自行复位 */
} slave_rpt_kind_t;

typedef struct
{
    slave_rpt_kind_t kind;
    uint8_t          panel;
} slave_report_t;

bool buff_slave_report_parse(const uint8_t in[8], slave_report_t *r);

/* ── 随机序列(老算法原样, 去 srand 化) ──
 * 用前请在上电时调用一次 srand(bsp_rng_get_seed()) (main.c 已做) */
void buff_random_small(uint8_t arr[], uint8_t length, uint8_t min, uint8_t max);
void buff_random_big(uint8_t arr1[], uint8_t arr2[], uint8_t length, uint8_t min, uint8_t max);

/* ── S3.1 从机接收路径(1ms 排空 → 目标状态幂等覆盖 + 事件计数) ──
 * 从机由 it.c 在 1ms 节拍里调用; 主机版不调用(主机收的是 0x160 回报)。
 * 依据: 大符轮次切换的两帧只隔 150ms, 4Hz 排空会吞掉熄灭帧 → 旧灯板不熄。
 * 见《从机代码重构计划》S3.1 / 《从机重构清单》§四。 */
typedef struct
{
    volatile uint32_t frames;       /* 解析成功的下行帧(0x161)累计数 */
    volatile uint32_t bad_frames;   /* 包头/范围不对被丢掉的帧数       */
    volatile uint8_t  seq;          /* 每收到一有效帧 +1: 4Hz/S5 据此判有新事件 */
    volatile uint8_t  cmd;          /* 0x01熄灭 0x02点亮 0x03复位 0x04全亮 */
    volatile uint8_t  is_big;       /* [2] 1=大符 */
    volatile uint8_t  is_blue;      /* [4] 1=蓝 0=红 */
    volatile uint8_t  panel_a;      /* [3] 1~5 */
    volatile uint8_t  panel_b;      /* [5] 大符第二块 1~5, 0=无 */
    volatile uint8_t  round;        /* [6] 已激活组数 0~5(进度条 n/5 用) */
    volatile uint16_t age_ms;       /* 距最近一有效帧多少 ms */
    volatile uint8_t  max_burst;    /* 单次 poll 排空到的最大帧数(主机全亮连发 5 帧属正常) */
    /* ── S5: 本机事件锁存(逐帧判"与我有关", 不能只靠上面的 cmd 镜像) ──
     * cmd 只保留"最后一条有效帧": 4Hz 窗口内别人的帧后到, 会吃掉发给本机的熄灭帧
     * —— 等于把老单缓冲覆盖缺陷搬进业务层。所以收帧当场按老 Decode 的匹配规则锁存:
     *   0x01/0x02 必须 [3]或[5]==本机板号; 0x03/0x04 广播不看板号。
     *   ★大符额外一条(差异表 #21): 0x02 点亮帧若 is_big 且本板不在 [3]/[5] 里 →
     *     按"熄灭"处理(隐式熄灭)。作用 = 熄灭帧万一丢失, 下一轮点亮帧到达时残留板
     *     自己灭掉, 不会出现"3 块板同时亮"。小符不加(打过的板要保持亮环数)。
     * 1ms 的 buff_tick() 消费 my_ev_valid 做状态转换(150ms 熄灯对不能塌进 4Hz 窗)。 */
    volatile uint8_t  my_ev_cmd;    /* 最近一条"与本板有关"的命令 */
    volatile uint8_t  my_ev_valid;  /* 1=有未消费事件 */
    volatile uint32_t my_ev_seq;    /* 每条命中/广播帧 +1(观测用) */
    volatile uint8_t  my_ev_implicit;   /* 1=该事件来自隐性熄灭(差异表 #21) */
    volatile uint32_t imp_off;      /* 差异表 #21: **清理掉一个还亮着的残留板**的次数
                                     * (= 熄灭帧确实漏过)。正常轮次恒为 0: 不在本轮的板
                                     * 早已被显式熄灭帧灭掉, 隐式熄灭只是幂等 no-op。 */
} buff_rx_t;

extern volatile buff_rx_t g_buff_rx;

void buff_can_rx_poll(void);        /* 1ms 调用: 排空 CAN1 下行帧 → g_buff_rx */

/* ── 调试观察量(长期保留) ── */
/* ⚠ 下面三个只在**主机构建**有值(写入点全在 #if BOARD_ROLE_MASTER 段内)；从机恒 0，
   从机收帧请看 g_buff_rx / buff_can_rx_poll()。 */
extern volatile uint32_t g_dbg_can1_rx_frames;   /* 收到的从机帧累计数 */
extern volatile uint8_t  g_dbg_buff_state;       /* 状态机当前状态 */
extern volatile uint8_t  g_dbg_buff_round;       /* 当前轮次 */

/* ── 大小符状态机(主机; 与《代码架构设计》3.3 转换表一致) ── */
typedef enum
{
    BB_RESET = 0,       /* 复位: 重取随机序列 + 发全复位帧(从机回休眠) */
    BB_IDLE,            /* 待激活: 尚未点亮第一组 */
    BB_LIT,             /* 已点亮: 等待被击中(第一组不设超时, 之后 2.5s) */
    BB_HIT_WINDOW,      /* 大符首击后的 1s 窗口 */
    BB_COOLDOWN,        /* 熄灭旧对 → 点亮新对的间隔 */
    BB_ALL_ON,          /* 全亮展示, 2.5s 后复位 */
} bb_state_t;

/* ── 从机状态机状态(S5; 与四种外观一一对应, light.h 的渲染场景用它) ── */
typedef enum
{
    SLV_SLEEP = 0,      /* 休眠: 5 条全灭; 老 state 1(0x01/0x03 进入, 重新武装击打) */
    SLV_ARMED,          /* 待激活: 靶面图案+箭头流动; 老 state 2(0x02 进入, 唯一允许被击中) */
    SLV_ACTIVATED,      /* 已激活: 环数显示+灯臂两条整条常亮(复刻口径, n/5 归 S8); 被击中进入并上报一次 */
    SLV_ALL_ON,         /* 全激活: 上/下两条点亮; 老 state 4(0x04 广播进入) */
} slv_state_t;

/* ── 状态机 ── */
void buff_init(void);
void buff_step(void);           /* 4Hz: 主机=排空+状态机+发帧; 从机=击打事件+上报+颜色刷新(S5) */
void buff_tick(float dt);       /* 1ms: 主机=2.5s/1s 计时; 从机=消费 my_ev 状态转换(S5) */

#endif /* BUFF_H */
