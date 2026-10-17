# 2027 Embedded Buff（能量机关固件）

2026/2027 赛季 RoboMaster 能量机关（大符/小符）固件。
STM32F405RGT6 + HAL，Keil MDK-ARM 工程，由 STM32CubeMX 6.15 生成。

主从共用同一块 PCB（原理图见 `docs/原理图/`），**分两个 CubeMX 工程分别构建**。

## 工程结构

```
2027_Embedded_Buff/
├── 2027_Embedded_Buff_Master/          # ★ 主机（中心板）：电机 + 中心灯带 + 灯板调度
│   ├── 2027_Embedded_Buff_Master.ioc   # CubeMX 配置（引脚/时钟/外设）
│   ├── Core/                           # 生成代码 + USER CODE 区
│   ├── Drivers/                        # HAL / CMSIS / DSP 库
│   ├── config/                         # 配置层：纯宏与 const 表（6 个头文件）
│   ├── bsp/                            # 板级支持包：ws2812 / can / switch / rng
│   ├── application/                    # 业务层：motor / buff / light
│   └── MDK-ARM/                        # Keil 工程（.uvprojx）
│
└── 2027_Embedded_Buff_Slave/           # ☆ 从机（打符灯板）：CubeMX ✅ + config 已从机化(S1)
    ├── README.md                       #   ← 从机入口页（改动要点 + 快速开始）
    ├── 2027_Embedded_Buff_Master.ioc   #   工程名沿用主机名(已定不改名，见《遗留待办》R3)
    ├── Core/ config/ bsp/ application/ #   与主机同构
    └── MDK-ARM/                        #   Drivers/ 未包含，CubeMX 生成时自动补齐
```

> 分层规则：`config`（只放数）→ `bsp`（硬件操作 + 中断收尾）→ `application`（业务逻辑）。
> 依赖只准向下；`Core/` 只动 `USER CODE` 区。详见 [`docs/代码架构设计.md`](docs/代码架构设计.md)。

## 快速开始（主机）

1. Keil 打开 `2027_Embedded_Buff_Master/MDK-ARM/2027_Embedded_Buff_Master.uvprojx`，Rebuild；
2. 需要改外设配置时用 STM32CubeMX 打开工程根目录的 `.ioc`（KeepUserCode 已启用）；
3. 烧录方式：SWD（PA13/PA14）。

## 当前进度

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 工程接入（config/bsp/application 分层骨架、TIM6 调度入口） | ✅ |
| P1 | config 六件套（时序/参数/标定值） | ✅ |
| P2 | `bsp_ws2812` 完整驱动（PWM+DMA / dirty-busy 握手 / 帧完成回调） | ✅ |
| P3 | 灯带点亮与通道定位（实测：主机仅 1 条灯带 = 中心 PA6/88 颗） | ✅ |
| P4 | `bsp_can`（双总线过滤器+双队列+零等待发送）+ buff 协议 pack/parse | ✅ |
| P5 | `motor`（双环 PID + 大符正弦前馈） | ✅ 代码完成（圈时间未标定，见待办 M1） |
| P6 | `buff` 大小符状态机 + 随机序列 | ✅ |
| P7 | 整机联调与验收（与旧从机对接 ✅ / 电机圈时间标定 ⬜ / 上电色序专项 ⬜ / 烤机 ⬜） | 🟡 |
| P8 | **从机重构**（见 `docs/从机代码重构计划.md`） | 🟡 S0~S3 ✅ / **S4~S6 代码 ✅（2026-10-10）**，台架统一在 S7，进度条 n/5 归 S8 专项 |

## 硬件要点速查（主机）

- MCU：STM32F405RGT6，HSE 8MHz → SYSCLK 168MHz（**APB1 Timer 84MHz**）
- **灯带（实测）：只有 1 条 —— "中心"，TIM3_CH1 / PA6 / 88 颗**（板上丝印标的是 DIN6）；
  PWM+DMA 驱动，bit 率 800kHz（PSC=2, ARR=34），T0H=10 / T1H=25，复位段 90bit。
  其余 5 个通道（PA7 / PB1 / PB6 / PB7 / PB8）实测**未接灯带**，CubeMX 中已裁掉
- CAN1（PA11/PA12）：主从互联，`0x161` 发指令 / `0x160` 收回报，1Mbps
- CAN2（PB12/PB13）：电机，`0x200` 发电流帧 / `0x201~204` 收反馈（波特率待实测确认）
- 拨码 ×3：`PD2` 大小符 / `PB3` 停转 / `PB4` 红蓝（**实测 OFF/下=低、ON/上=高**；高=转动/蓝/大符）
- 板卡身份 ID1~3：`PC11 / PC12 / PB9`（外部 10k 上拉 + 三挡拨动开关拉低）
- 1ms 调度：TIM6（PSC=83, ARR=999），中断内跑电机环 + 灯带刷新 + 计时；20ms 采拨码；250 分频的 4Hz 逻辑帧跑状态机与灯效

## 调试小工具

| 变量 | 位置 | 用途 |
|---|---|---|
| `g_sched_tick_cnt` | `Core/Src/stm32f4xx_it.c` | 调度心跳：1ms 中断每 tick +1，为 0 或不动 = 中断没进 |
| `g_dbg_ws_scan_idx` | `application/light.c` | 灯带通道扫描：`DEBUG_TEST_MODE=1` 时该值 = 当前点亮灯带的索引（0xFF=未扫描） |
| `g_dbg_buff_state` / `g_dbg_buff_round` | `application/buff.c` | 大小符状态机状态与轮次（对照转换表看流程） |
| `g_dbg_can1_rx_frames` | `application/buff.c` | 收到的从机回报帧累计数 |
| `g_buff_rx`（从机） | `application/buff.c` | 从机收帧快照：`frames`/`bad_frames`/`seq`/`cmd`/`round`/`age_ms`/`max_burst` + `my_ev_*` 本机事件锁存 —— **S7 组 1 的主判据** |
| `g_dbg_slv`（从机） | `application/buff.c` | 从机状态机快照：`state`/`circle`/`ev_cnt`/`hit_ev`/`mishit_ev`/`report_hit`/`report_mishit` —— **S7 组 3 主判据** |
| `g_dbg_hit`（从机） | `application/hit_detect.c` | 击打快照：`fire_cnt`/`mishit_cnt`/`ignored`/`stray`/`last_pin`/`last_circle`/`age_ms` —— **S7 组 2 主判据（重点核对 RDET9 几环）** |
| `g_dbg_pat`（从机） | `application/light.c` | 图案库快照：`scene`/`circle`/`phase`/`phase_div`/`test`（白光扫描索引仍叫 `g_dbg_ws_scan_idx`）—— **S7 组 4 主判据** |
| `g_dbg_motor` | `application/motor.c` | 电机环快照：`fb_frames`(反馈帧累计, 判断速度环是否闭环)、`fb_speed`/`fb_angle`、`target`/`position`/`delat`、`cmd_current` |

## 文档（`docs/`）

| 文档 | 内容 |
|---|---|
| [重构工作记录](docs/重构工作记录.md) | 三段复盘：老代码有什么问题、重构后改了哪些、结果与完成度（含 Git 树截图与台架视频位置）。汇报可直接用 |
| [代码架构设计](docs/代码架构设计.md) | 分层架构、模块职责、依赖规则、调度设计（**开发前先读**） |
| [重构对照-旧代码与新设计](docs/重构对照-旧代码与新设计.md) | 2025 老固件逐行分析、标定数据继承核对表 |
| [主机CubeMX配置清单](docs/主机CubeMX配置清单.md) | CubeMX 每一项配置的依据、待确认项、自检清单 |
| [CubeMX裁剪说明](docs/CubeMX裁剪说明.md) | 主机为何/如何裁掉冗余外设 + 生成后校验法（DMA 方向坑）；从机加回明细已并入下方《操作说明》 |
| [白光测试指南](docs/白光测试指南.md) | 主机板 2026 逐路白光定位法（换板/改线时参考；**从机看《从机灯带自检与排障》**） |
| [P3调试指南](docs/P3调试指南.md) | 灯带不亮的 debugger 排查流程（含 DMA 方向踩坑记录） |
| [代码重构计划](docs/代码重构计划.md) | 主机 P0~P6 分阶段实施记录（**已全部完成，留档**；未做完的在《遗留待办》§一） |
| [大符思路](docs/大符思路.txt) | 2025 赛季需求与修改记录（原维护者手记） |
| [遗留待办与开放问题](docs/遗留待办与开放问题.md) | **总览**：主机/从机/硬件三类待办 + 仓库事项（完成即销项） |
| [从机代码重构计划](docs/从机代码重构计划.md) | **从机路线图**：阶段 S0~S8、里程碑、风险与 DoD、可复用资产清单（做什么/按什么顺序） |
| [从机重构清单](docs/从机重构清单.md) | **从机唯一明细出处**：逐文件改动(§三)、协议时序(§四)、与老从机差异(§五)、逐项验收 ①~⑭(§六)、协议速查(§七) |
| [从机CubeMX操作说明](docs/从机CubeMX操作说明.md) | 从机 CubeMX 逐步操作（照着点）+ 每步校验命令（**✅ 已完成并校验，留档**） |
| [从机灯带自检与排障](docs/从机灯带自检与排障.md) | 可选排障工具：逐条点亮定位"某条灯带不亮/段位不对"（**从机未接拨码，只能用 `DEBUG_TEST_MODE` 宏触发**）|
| [从机S7台架测试指南](docs/从机S7台架测试指南.md) | **从机 S7 开工必读**：五组串行台架验收的操作步骤、Watch 判据与记录表（不用 CAN 分析仪）|
| [能量机关规则](docs/能量机关规则.pdf) | 2026 规则 5.5.2.1 能量机关状态（点亮/2.5s/1s 窗口/熄灭≤200ms/激活失败） |
| [原理图](docs/原理图/) | 主从共用板原理图（PDF + SVG，V1.0） |

> 📌 **`docs/` 是文档的唯一母版**（随代码版本化）。上级目录 `2026_Embedded-Buff_new/` 曾有的 7 份旧副本已删除；
> 那里只留第三方教程 `../CubeMX基础配置.md`(+zip)、其配图 `../图片和附件/`、原理图导出与架构图 HTML。
> **改设计文档只改 `docs/`**，不要再开第二份。
