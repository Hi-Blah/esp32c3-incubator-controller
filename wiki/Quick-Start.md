# Quick start

1. Obtain an ESP32-C3 SuperMini with 4 MB Flash, a USB data cable, supported modules and a trusted 2.4 GHz network. See [BOM](Hardware.md).
2. Keep all 24 V loads disconnected. Set your NAS origin in firmware/IncubatorController/web_entry.h. Generic example addresses in this repository must be replaced.
3. Follow [the exact build settings](Firmware.md). Upload with flash erase disabled when upgrading an existing board.
4. Open USB serial at 115200 baud. Send each command separately: status, diag, device info, samples info. Confirm logic=PASS, nvs=1 and outputs OFF. Use logs init only for a first-use/uninitialized filesystem; it can format that partition when mounting fails.
5. Read the generated web/AP secrets locally with wifi key. Connect to the Incubator-… AP and open http://192.168.4.1/local . Configure Wi-Fi; then reconnect your browser to the same LAN and use the device's reported IP. Do not paste the secret output into issues.
6. [Prepare the NAS stack](Deployment.md) using the six-digit device ID, then enter the incubator_device MQTT account in the local device UI. Lock both channels before changing MQTT settings.
7. Confirm a current firmware version and new uptime in Home Assistant and the NAS /device/ page. A retained old state is not evidence of a current connection.
8. Test each output while disconnected from the load, then connect one fused load and verify polarity. Test daily/cyclic schedules and recovery before enabling both loads.
9. Add light sensors only with power disconnected. Verify that ADC values change, then calibrate dark/full-bright baselines and configure NAS mail.
10. Run the [offline replay acceptance test](Validation.md). Do not erase or reinitialize storage to fix a backlog.

