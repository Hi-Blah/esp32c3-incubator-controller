# MQTT 与 Home Assistant

在 HA 中使用普通 MQTT 集成，连接 NAS broker，账号使用独立的 incubator_ha，并启用自动发现。发现主题为 homeassistant/…/incubator_设备编号/…/config。设备编号是保存的六位逻辑 ID，不是大家共用的硬编码；通过 device info 查看。复制多块板时应生成不同编号，只有替换已经离线的原设备时才保留原编号。

| incubator/设备编号 下的主题 | 方向 | 作用 |
|---|---|---|
| state | 板 → broker | 约 2 秒实时状态，保留消息；标定时放慢 |
| config | 板 → broker | 保留的程序与恢复配置 |
| availability | 板 → broker | online/offline 连接状态，不能单独证明停电 |
| command/light1、light2、arm、pwm1、pwm2 | HA → 板 | ON/OFF，拒绝 retained 控制 |
| command/duty1、duty2、frequency1、frequency2 | HA → 板 | 有范围限制的整数 |
| progress、recovery/request、recovery/reply | 双向 | 带身份匹配的 NAS 辅助续跑 |
| events、telemetry | 板 → NAS | 原有有界事件队列 |
| analysis/# | 双向 | NAS 模型、心跳、受限网页控制及回复 |
| samples | 板 → NAS | 新版 v1 历史快照，QoS1，不保留 |
| samples/ack | NAS → 板 | 完成落库后的精确回执，不保留 |

历史采样包括 boot_id、boot_order、字符串序号 seq、uptime_ms、epoch_ms、sensor_uptime_ms、cfg、温湿度、两路 raw/frequency/duty/mode、outage_id、offline_elapsed_ms、period_s 及 flags。标志位：温湿度有效=1、光敏1有效=2、光敏2有效=4、有绝对时间=8、允许运行=16、输出1=32、输出2=64、PWM1=128、PWM2=256、仅时间锚点=512、NAS时间=1024、断联采样=2048、额外保留的重要记录=4096。无效温湿度为 null；ADC 原始值不是 lux。

回执按 v、boot_id、seq、uptime_ms 的顺序发送。NAS 提交数据库后才发布；设备只接受与当前队首完全一致的回执，旧消息、retained 或不匹配的回执不能释放其他记录。

原始光敏实体由固件发现，派生模型/诊断实体由 NAS 发现。模块和实体可选，不把实体总数写死。初次开关、PWM 测试先断开 24 V；网页和 HA 命令仍遵循设备锁定及恢复规则。

版本看起来陈旧时，应检查实时 uptime 和 availability，而不仅是 retained 状态。温度缺失可能是传感器无效或可用性条件未满足，不一定是 broker 故障。NAS 配置 API 出错还可能是缺少 config 主题订阅/权限。按[部署说明](Deployment-zh-CN.md)检查，新增 samples 权限时不要覆盖原权限。

HA 经反向代理访问时，开启 use_x_forwarded_for，并只信任 HA 日志实际看到的代理地址，不信任所有地址。已有配置应合并到同一个 http: 块，验证后重启。参照[HA 官方 HTTP 文档](https://www.home-assistant.io/integrations/http/)。

