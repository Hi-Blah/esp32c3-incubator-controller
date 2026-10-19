# Connected-2.2.4: MQTT recovery candidate

[中文](firmware-2.2.4-zh-CN.md)

This candidate combines the [2.2.3b local HTTP fixes](firmware-2.2.3b.md) with targeted MQTT reconnection changes. Keep NAS-Recorder-2.2.3a and existing Compose, credentials, ACL and database files. No partition, NVS schema, topic or recorder migration is required. Hardware acceptance is pending.

## Evidence and scope

A 2.2.3 installation stopped delivering live states for 70 minutes 12 seconds, with the same boot ID before and after. The last pre-gap state reported 27,556 bytes free, a historical minimum of 7,320 and a largest block of 16,372. The first recovered state reported 20,500 free, a minimum of 1,332 and a largest block of 7,668. The low minimum was first observed after the gap; these samples cannot establish whether memory pressure caused the interruption or resulted from retries. Broker logs showed repeated device-side connection closures. They do not identify the board's internal stop reason.

The source review found that a queued PUBACK was processed after the timeout check, and an old pending ACK deadline could outlive a TCP reconnection. Every reconnect also restarted a thirteen-message discovery sequence before publishing availability or live state. These are recovery defects; fixing them does not establish the original network failure's cause.

## Changes

- Consume a matching queued PUBACK before evaluating its deadline. A true SDK connection event renews a pending packet's 30-second ACK window; HA birth messages do not. A 180-second total packet lifetime prevents repeated short connections from deferring recovery indefinitely. A disconnected client uses a separate, bounded 120-second watchdog before task/transport recreation, allowing several SDK retries instead of rebuilding after 30 seconds. Memory protection still applies.
- Publish fresh QoS0 state independently of the discovery sequence. Publish retained availability and current program configuration ahead of background discovery. Discovery shares round-robin slots with progress, event records and Flash samples, with at least two seconds between acknowledged entries. Partial discovery sweeps continue across SDK reconnects; a completed sweep is refreshed on reconnection or an HA birth message.
- Preserve event ordinals until a matching broker PUBACK. Flash sensor samples retain their separate NAS database-commit receipt requirement. A reconnect is never treated as an acknowledgement.
- Add read-only `mqtt diag` over authenticated BLE and USB, including last client-release reason, time, heap, largest block, pending ACK age and cached I/O failure context. No passwords, extra MQTT topics, dynamic packet buffers or recurring diagnostic payloads are added.
- Include the lightweight `/api/entry`, configured-NAS CSP allowance, pre-response memory admission and bounded HTTP memory waiting from 2.2.3b.

Lamp control, PWM, daily/cyclic timing, restart preferences and offline sensor storage are unchanged. No automatic board reset is introduced. Existing firmware build settings and partitions remain valid.

## Validation and acceptance

Regression cases cover queued ACKs after a delayed loop, retained record identity across reconnects, stale availability ACKs, HA birth messages, completion of discovery over short sessions, fair background delivery and timer rollover. CI compiles and links the firmware with stack-usage output enabled. Local stack-usage review found a 2,048-byte frame for `mqttService` (previously 1,904), and a 672-byte frame for the new diagnostic formatter. These are function frames, not complete call-chain or runtime peak measurements. The checks do not simulate the campus network, hardware heap fragmentation or an actual multi-hour disconnection.

After flashing, first observe NAS/HA without board browser tabs, check the new version and unchanged boot identity during any outage, and verify that Flash backlog drains after reconnection. Then test the entry page and `/local` separately. A busy local page may return HTTP 503 while the experiment continues. If another interruption occurs, `mqtt diag` can be read through BLE without restarting the board; compare its stop/I/O context with broker logs. Do not interpret the historic minimum alone as proof of the trigger.
