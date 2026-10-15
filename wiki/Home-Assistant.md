# MQTT and Home Assistant

Use the normal MQTT integration with the NAS broker and the separate incubator_ha account. Discovery must be enabled. The firmware announces under homeassistant/…/incubator_DEVICE/…/config. Device identity is a saved six-hex logical ID, not a hardcoded shared public ID. Check device info; cloning boards requires distinct IDs unless deliberately replacing one offline device.

| Topic under incubator/DEVICE | Direction | Purpose |
|---|---|---|
| state | MCU → broker | Retained real-time status, roughly 2 s; slower during calibration |
| config | MCU → broker | Retained program/recovery configuration |
| availability | MCU → broker | online/offline connection status, not proof of power failure |
| command/light1, light2, arm, pwm1, pwm2 | HA → MCU | ON/OFF; retained commands rejected |
| command/duty1, duty2, frequency1, frequency2 | HA → MCU | Bounded integer values |
| progress, recovery/request, recovery/reply | Both | Matched NAS-assisted cycle recovery |
| events, telemetry | MCU → NAS | Original bounded event queue |
| analysis/# | Both | NAS models, heartbeat and bounded web-control requests/replies |
| samples | MCU → NAS | New v1 historical snapshots, QoS1, never retained |
| samples/ack | NAS → MCU | Exact unretained durable-commit receipt |

Each historical sample includes boot_id, boot_order, a decimal-string seq, uptime_ms, epoch_ms, sensor_uptime_ms, cfg, temperature, humidity, paired raw/frequency/duty/mode, outage_id, offline_elapsed_ms, period_s and flags. Flag bits are SENSOR_VALID=1, PHOTO1_VALID=2, PHOTO2_VALID=4, HAS_EPOCH=8, ARMED=16, OUTPUT1=32, OUTPUT2=64, PWM1=128, PWM2=256, ANCHOR_ONLY=512, NAS_TIME=1024, OUTAGE=2048, IMPORTANT=4096. Invalid environment values are null. These raw ADC counts are not lux.

The receipt wire format is intentionally small and ordered: v, boot_id, seq, uptime_ms. NAS publishes it only after durable commit. A receipt must identify the current queue head exactly; an old, retained or mismatching receipt cannot delete another sample.

Optical raw entities are published by firmware. Derived optical model/diagnostic entities are announced by NAS; do not hardcode an entity count because optional modules and discovered entities vary. Test lights and PWM with 24 V disconnected first. All UI commands remain subject to the device's arm/recovery rules.

For a stale version, inspect live uptime and availability, not only retained state. A missing temperature reading can mean invalid sensor data or availability conditions; it does not prove broker failure. A configuration API error can mean a missing retained config subscription/ACL. See [deployment](Deployment.md) and preserve existing permissions when adding sample topics.

To use Home Assistant behind a reverse proxy, configure use_x_forwarded_for and trust only the proxy address actually seen in HA logs. Do not trust every address. Merge into a single http: block, validate configuration and restart. See [official HA HTTP documentation](https://www.home-assistant.io/integrations/http/).

