# Version history / 版本记录

The initial public tree is a clean source snapshot. This table describes private development milestones; it does not claim a reconstructed Git history or that every intermediate artifact is suitable for use.

| Version family | Main changes / 主要内容 | Status / 状态 |
|---|---|---|
| 1.x Advanced | Local manual/daily/cycle, PWM, Wi-Fi/BLE, early recovery / 本地灯控及早期恢复 | Historical prototype; not recommended for new deployments |
| 2.0.0–2.0.9 | MQTT/HA, event logging, diagnostics, NAS recovery / 联网、日志、NAS 续跑 | Several stack/heap/network issues investigated; old packages excluded |
| 2.0.10 | Early working rollback comparison / 早期可用对照基线 | Limited-memory and queue issues remained |
| 2.1.0–2.1.2 | Per-channel policies, optical features, UI changes / 独立策略、光反馈、页面 | Memory/network regression reports; not the public starting point |
| 2.2.0 | NAS analysis/notifications and modular acquisition / NAS 分析、通知、采集模块 | Malformed MQTT packet reports; superseded |
| 2.2.1 | MQTT transport framing and bounded resource fixes / 传输帧和资源限制修复 | Superseded |
| 2.2.2 + NAS 2.2.2a/b/c | NAS main UI, state/config ACL fixes, manual-ON optical alarm and SMTP delivery fixes / NAS 主界面、权限、光照报警、发件修复 | User-selected stable prototype baseline; cumulative NAS source includes c |
| 2.2.3 candidate | Adaptive 1s/3s/10s Flash journal, direct NAS clock sync, delayed outage notices, NAS commit receipts, per-boot time reconstruction, centralized pins, reproducible public layout / 原始采样缓存、入库确认、分段时间、针脚集中及公开整理 | Automated checks separate from pending hardware acceptance |

See [acceptance and limitations](wiki/Validation.md) / [验证与限制](wiki/Validation-zh-CN.md). The Docker installation is upgraded cumulatively; it is not rolled back when firmware comparisons are performed.

