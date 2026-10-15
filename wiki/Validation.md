# Acceptance, limits and troubleshooting

The prototype reference pair is Connected-2.2.2 plus cumulative NAS-Recorder-2.2.2c. Reference checks covered HA data/control, NAS records/CSV, NAS-assisted restart recovery and delivered optical-fault mail. This is a prototype acceptance report, not an endurance or safety certification.

Connected-2.2.3 adds the journal and reproducible build layout. Automated checks are recorded separately in docs/verification.md. Hardware acceptance is pending until these checks are performed on the actual intended wiring/load:

1. Start with USB only, both channels locked. Verify version, config retention, logic/NVS, Flash filesystem size, samples info and no resets.
2. Verify each sensor's validity and changing raw values. Test one output and PWM, then two outputs under the intended fused load. Check measured current and temperature over the planned operating duration.
3. With NTP already valid, stop only the recorder for about two minutes while keeping the broker and board powered. After heartbeat expiry, expect 1-second capture and increasing pending, then durable replay after recorder restart. Also test >3min and >15min: period changes to 3s/10s, thinned increases, critical changes remain, covered stays zero when space permits.
4. Repeat with broker/network unavailable. Restore it and compare capture times with receipt times; history should fill the gap without replaying old commands or alerts.
5. Restart the ESP32 during the outage. Repeat several times. Verify distinct boot segments in ordered CSV, retained pre-reset data and valid time-quality labels. If a segment never had an anchor, its absolute time must remain blank.
6. Simulate NTP unavailable while NAS works. Check nas labels; restoring NTP should refine that same boot without moving other boots.
7. Lose a receipt/restart after ingestion. Confirm deduplication and no extra database rows/alerts. Confirm backlog and cover counts remain visible.
8. Test full/torn storage using the host harness and then controlled hardware tests; do not fill or corrupt the only experimental device. Never use logs init as a retry/queue-reset step.
9. Test iPad Safari time pickers, collapsed mode sections, both independent saves, graphs, legend/tooltips and local fallback with NAS unavailable.
10. Verify full automatic calibration, a real missing-light fault, manual-OFF exemption, daily/cycle OFF-with-light fault and SMTP delivery. Mark any untested matrix explicitly.

Known limits: weak/interfered Wi-Fi still causes timeouts; larger Flash cannot repair RF loss or increase working RAM. Low-duty high-frequency optical output is hardware-dependent. Flash history is finite, not a lossless indefinite logger; final unflushed writes/counters may be lost. Reboots consume partial pages. No battery-backed RTC means unanchored offline reboot gaps cannot be reconstructed. HA history is not backfilled. INA226/current-based protection and a full high-load thermal validation are not implemented/complete.

Troubleshooting: inspect status, diag, memory, wifi info, mqtt info, samples info and NAS broker/recorder/mail logs. Remove secrets before sharing. heap_min is a historical low watermark; it does not increase after memory recovers. A connected Wi-Fi flag proves association, not reliable TCP. Broker malformed packet is a protocol symptom; old retained data is not a new sample. Keep MQTT disabled during isolated memory tests and change one condition at a time.

