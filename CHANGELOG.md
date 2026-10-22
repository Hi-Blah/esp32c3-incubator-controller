# Version history / 版本记录

Versioned source packages and their independent installation guides are listed below. Firmware and NAS are separate version lines; a firmware comparison does not require downgrading a production recorder. Read each archive's known limitations and manifest before selecting it. Unversioned examples are named explicitly instead of assigning retrospective release numbers.

各版本源码包与独立安装说明如下。固件与 NAS 分开编号；做固件对照无需同步回退现役记录器。选择前核对包内已知问题和验证范围；未编号示例不追溯编造版本号。

See [all downloads](archive/README.md) / [全部下载](archive/README.zh-CN.md).

## Licensing / 许可证

2026-10-23: project-original source, documentation, diagrams and archived original revisions are licensed under Apache-2.0. Historical source packages include the license and attribution files; their manifests and download checksums have been refreshed. Program sources are unchanged. Third-party licenses are preserved.

2026-10-23：项目原创源码、文档、图示及历史原创修订采用Apache-2.0。历史源码包补齐许可与署名文件，并更新清单及下载校验值；程序源码保持原样，第三方许可保留。

## Current validation / 当前验证

**2026-10-22 — Connected-2.2.4 + NAS-Recorder-2.2.3a:** complete firmware build and source/backend CI passed. The maintainer reported over ten minutes of stable hardware operation after flashing, with no issue observed. Marked as initial hardware validation; extended uptime, outage recovery and replay validation remain pending.

**2026-10-22 — Connected-2.2.4 + NAS-Recorder-2.2.3a：** 完整固件构建及源码/后端CI已通过。维护者反馈刷入后平稳运行十几分钟，暂未发现问题。标记为初步实机验证通过，长时间运行、断联恢复及补传验收仍待完成。

Details / 详情：[English](docs/firmware-2.2.4.md) / [中文](docs/firmware-2.2.4-zh-CN.md)。

## Firmware / 固件

| Version | Main changes / 主要内容 | Limitations / 限制 |
|---|---|---|
| [Advanced-1](archive/firmware/Advanced-1/README.md) | Manual/daily/cycle programs, PWM, BLE and Wi-Fi. / 手动、每日、循环程序、PWM、蓝牙和 Wi-Fi。 | Early prototype; superseded by 1.1. / 早期原型，后续由 1.1 替代。 |
| [Advanced-1.1](archive/firmware/Advanced-1.1/README.md) | Early recovery policy and local network refinements. / 早期断电恢复策略和本地联网调整。 | Historical local-only architecture; no MQTT or NAS. / 历史本地架构，不含 MQTT 或 NAS。 |
| [Connected-2.0](archive/firmware/Connected-2.0/README.md) | First MQTT discovery, event log and diagnostic integration. / 首次集成 MQTT 自动发现、事件日志和诊断。 | Known controller stack-protection resets; use for investigation only. / 已知控制任务栈保护复位，仅供研究对照。 |
| [Connected-2.0.1](archive/firmware/Connected-2.0.1/README.md) | Controller stack fix and additional diagnostics. / 控制任务栈修复及诊断增强。 | MQTT heap pressure and network failures were still reported. / 仍有 MQTT 内存压力及联网故障报告。 |
| [Connected-2.0.2](archive/firmware/Connected-2.0.2/README.md) | Asynchronous MQTT configuration and network probing. / 异步应用 MQTT 配置及网络探测。 | Web latency, logger backlog and low-memory failures remained. / 仍有网页延迟、日志积压和低内存故障。 |
| [Connected-2.0.3](archive/firmware/Connected-2.0.3/README.md) | HTTP worker/transfer and log diagnostic refinements. / HTTP 任务、网页传输和日志诊断改进。 | MQTT coexistence and persistent queue problems remained. / 仍需注意 MQTT 共存及持久队列问题。 |
| [Connected-2.0.4](archive/firmware/Connected-2.0.4/README.md) | Bounded MQTT flow and PWM memory fixes. / 限制 MQTT 数据流并修正 PWM 内存问题。 | Wireless reconnect and heap fragmentation were observed. / 曾观察到无线重连及堆碎片问题。 |
| [Connected-2.0.5](archive/firmware/Connected-2.0.5/README.md) | Radio/network controls and availability refinements. / 无线参数控制和可用性改进。 | Weak radio links can still interrupt MQTT. / 弱无线连接仍可能中断 MQTT。 |
| [Connected-2.0.6](archive/firmware/Connected-2.0.6/README.md) | Independent dual-channel use and local interface refinements. / 双路同时使用及本地界面调整。 | Predates NAS-assisted progress recovery and optical feedback. / 早于 NAS 进度辅助恢复及光反馈。 |
| [Connected-2.0.7](archive/firmware/Connected-2.0.7/README.md) | HA PWM controls and NAS-assisted cycle recovery. / HA PWM 控制及 NAS 辅助循环恢复。 | Early recorder protocol; no optical ADC feedback yet. / 早期记录器协议，尚无光敏 ADC 反馈。 |
| [Connected-2.0.8](archive/firmware/Connected-2.0.8/README.md) | Compile-time modules and two optical ADC inputs. / 编译时模块开关及两路光敏 ADC。 | Historical memory/network limits and queue backlog remained. / 仍受历史内存、网络限制和队列积压影响。 |
| [Connected-2.0.9](archive/firmware/Connected-2.0.9/README.md) | Charts, quick optical calibration and manual recovery. / 曲线、快速光照校准及手动状态恢复。 | Event backlog and weak-network memory degradation were reported. / 曾报告事件积压及弱网下内存退化。 |
| [Connected-2.0.10](archive/firmware/Connected-2.0.10/README.md) | Event replay and recovery refinements. / 事件补传及恢复逻辑改进。 | Comparison release; not immune to heap/network or queue failures. / 对照版本，仍可能有内存、联网和队列故障。 |
| [Connected-2.1.0](archive/firmware/Connected-2.1.0/README.md) | Per-channel recovery, optical calibration and NAS notifications. / 每路恢复策略、光照标定及 NAS 通知。 | Reported RAM/network regressions; historical experimental release. / 有 RAM、联网退化报告，属于历史实验版本。 |
| [Connected-2.1.1](archive/firmware/Connected-2.1.1/README.md) | Daily restart recovery and filesystem diagnostics. / 每日定时重启恢复及文件系统诊断。 | Reported AP/web instability and memory pressure. / 有热点、网页不稳定和内存压力报告。 |
| [Connected-2.1.2](archive/firmware/Connected-2.1.2/README.md) | AP and bounded HTTP-transfer diagnostics. / 热点及受限 HTTP 传输诊断。 | Memory-protection MQTT stops and web timeouts were reported. / 有 MQTT 内存保护停止和网页超时报告。 |
| [Connected-2.1.2-WiFiDiag](archive/firmware/Connected-2.1.2-WiFiDiag/README.md) | BLE-disabled comparison for RAM and wireless diagnosis. / 关闭 BLE 的 RAM 及无线对照版本。 | Diagnostic variant; BLE is intentionally unavailable. / 诊断变体，蓝牙不可用属于预期行为。 |
| [Connected-2.2.0](archive/firmware/Connected-2.2.0/README.md) | Move analysis, fitting and mail decisions to NAS. / 将分析、拟合和邮件判断移至 NAS。 | Broker malformed-packet disconnects were reported; superseded. / 曾发生 broker malformed packet 断开，已被替代。 |
| [Connected-2.2.1](archive/firmware/Connected-2.2.1/README.md) | MQTT framing, state heartbeat and bounded transfer fixes. / MQTT 帧、状态心跳和受限传输修复。 | Superseded by 2.2.2 resource/lifecycle refinements. / 后续由 2.2.2 的资源及生命周期改进替代。 |
| [Connected-2.2.2](archive/firmware/Connected-2.2.2/README.md) | NAS primary UI, bounded coexisting services and reconnect control. / NAS 主界面、服务共存资源限制及重连控制。 | Prototype reference release with NAS 2.2.2c; no raw Flash sensor journal. / 与 NAS 2.2.2c 配套的原型参考版本，不含 Flash 原始传感器缓存。 |
| [WiFi-unversioned](archive/firmware/WiFi-unversioned/README.md) | Early hardware/local control example. / 早期硬件或本地控制示例。 | Unversioned example; inspect its own source and pin map. / 未编号示例，按其源码和针脚表使用。 |
| [HardwareCheck-unversioned](archive/firmware/HardwareCheck-unversioned/README.md) | Early hardware/local control example. / 早期硬件或本地控制示例。 | Unversioned example; inspect its own source and pin map. / 未编号示例，按其源码和针脚表使用。 |
| [IncubatorBasic-unversioned](archive/firmware/IncubatorBasic-unversioned/README.md) | Early hardware/local control example. / 早期硬件或本地控制示例。 | Unversioned example; inspect its own source and pin map. / 未编号示例，按其源码和针脚表使用。 |
| [IncubatorWiFiNtpRetry-unversioned](archive/firmware/IncubatorWiFiNtpRetry-unversioned/README.md) | Early hardware/local control example. / 早期硬件或本地控制示例。 | Unversioned example; inspect its own source and pin map. / 未编号示例，按其源码和针脚表使用。 |
| [Connected-2.2.3](archive/firmware/Connected-2.2.3/README.md) | Adaptive Flash journal, NAS clock and durable sample receipts. / 自适应 Flash 缓存、NAS 校时及采样入库回执。 | Historical; recorder stalls and device HTTP/MQTT recovery issues were reported. See 2.2.3a/2.2.4. / 历史版本，有记录器堵塞及本机网页/MQTT恢复问题报告，参见2.2.3a/2.2.4。 |
| [Connected-2.2.3b](docs/firmware-2.2.3b.md) | Local HTTP resource handling; integrated into 2.2.4. / 本机网页资源处理，已合入2.2.4。 | Development hotfix candidate; not independently hardware-accepted. / 开发修复候选版，未单独实机验收。 |
| [Connected-2.2.4](docs/firmware-2.2.4.md) | Reconnect ACK handling, live data before background discovery and BLE diagnostics, including 2.2.3b web fixes. / 重连确认处理、优先实时数据、蓝牙诊断，含2.2.3b网页修复。 | CI passed; initial hardware observation over ten minutes passed on 2026-10-22; extended validation pending. / CI通过；2026-10-22十几分钟初步实机观察通过，长期验收待完成。 |

## NAS recorder / NAS 记录器

| Version | Main changes / 主要内容 | Limitations / 限制 |
|---|---|---|
| [NAS-Recorder-2.0.7](archive/nas/NAS-Recorder-2.0.7/README.md) | Superseded historical runtime; assess compatibility before use. / 历史版本，使用前需核对兼容性。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.0.8](archive/nas/NAS-Recorder-2.0.8/README.md) | Superseded historical runtime; assess compatibility before use. / 历史版本，使用前需核对兼容性。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.1.0-local](archive/nas/NAS-Recorder-2.1.0-local/README.md) | Superseded historical runtime; assess compatibility before use. / 历史版本，使用前需核对兼容性。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.1.0](archive/nas/NAS-Recorder-2.1.0/README.md) | Optical records and notifications; predates the full device portal. / 支持光照记录和通知，早于完整设备控制页。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.0](archive/nas/NAS-Recorder-2.2.0/README.md) | NAS optical analysis and model dashboard; not the later complete device portal. / 提供 NAS 光照分析和模型仪表盘，尚不是后来的完整设备控制页。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.2](archive/nas/NAS-Recorder-2.2.2/README.md) | Initial full device portal; configuration-topic parsing/ACL errors require the a/c fixes. / 首次提供完整设备控制页，配置主题解析和权限问题需采用 a/c 修复。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.2a](archive/nas/NAS-Recorder-2.2.2a/README.md) | Includes configuration-topic/ACL gateway fixes. / 包含配置主题、ACL 和控制网关修复。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.2b](archive/nas/NAS-Recorder-2.2.2b/README.md) | Includes manual-ON optical alarms and notification UI fixes; SMTP delivery issue remained. / 包含手动开灯无光报警和通知界面修复，SMTP 投递问题尚未修复。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.2c](archive/nas/NAS-Recorder-2.2.2c/README.md) | Cumulative a/b/c code, including SMTP envelope/body submission fixes. / 累计包含 a/b/c 修复，包括 SMTP 信封与正文投递。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |
| [NAS-Recorder-2.2.3](archive/nas/NAS-Recorder-2.2.3/README.md) | Adds durable sample receipts, historical backfill and >180-second disconnect notices. / 新增采样入库回执、历史补传和超过 180 秒断联提示。 | Cumulative source; use an isolated database for historical comparisons. / 累计源码，历史对照须使用独立数据库。 |

[NAS-Recorder-2.2.3a](docs/nas-2.2.3a.md) bounds clock repair work to avoid recorder database stalls; it remains the current NAS line used with firmware 2.2.4. / NAS-Recorder-2.2.3a限制时间回填工作量，修正记录器数据库堵塞，仍为2.2.4配套的现役NAS版本。

Current acceptance scope: [English](wiki/Validation.md) / [中文](wiki/Validation-zh-CN.md).
