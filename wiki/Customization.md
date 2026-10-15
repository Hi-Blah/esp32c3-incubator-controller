# Pins, modules and parameters

The public layout centralizes wiring in firmware/IncubatorController/board_config.h. Default behavior is unchanged. Custom wiring has not inherited the default hardware acceptance result.

| Change | Edit | Code that consumes it |
|---|---|---|
| MOS outputs | INC_LIGHT1_PIN / INC_LIGHT2_PIN in board_config.h | application.cpp LIGHT_PIN[], configurePwm(ch), drive(ch,on); ch is zero-based |
| OLED I2C | INC_I2C_SDA_PIN / INC_I2C_SCL_PIN | applicationSetup() calls Wire.begin(...,400000); address auto-probes 0x3c/0x3d |
| DHT DATA | INC_DHT_PIN | BoundedDht11::pin in bounded_dht.h; do not change RMT pulse thresholds to move a pin |
| Light detector AO | INC_PHOTO1_PIN / INC_PHOTO2_PIN | light_feedback.cpp ADC setup and sampling |
| Buttons | INC_PAGE_BUTTON_PIN and optional channel button macros | application.cpp serviceButtons(), INPUT_PULLUP |
| Enable modules | INC_ENABLE_BLE / DHT / OLED / PHOTO in module_config.h | Compile-time exclusion; PHOTO=0 returns GPIO0/1 to channel buttons |
| Flash journal | INC_ENABLE_SAMPLE_JOURNAL, INC_SAMPLE_INTERVAL_MS, INC_SAMPLE_FAST_PHASE_MS, INC_SAMPLE_SLOW_PHASE_MS | sample_journal.cpp; default 1 and 10000 ms; whole seconds, minimum 1000 ms; offline phases 1s/3s/10s at 3min/15min |
| Timezone | INC_TIMEZONE in module_config.h | POSIX zone; CST-8 means UTC+8; daily schedules use it |
| NAS page link | INC_NAS_WEB_ORIGIN in web_entry.h | Small MCU portal; set your NAS URL before building |
| MQTT endpoint / Wi-Fi | Device local UI | NVS settings; never bake passwords into public source |
| Programs / restart / PWM | Per-channel UI or commands | Saved configuration; no source edit required |

Example: with all default modules enabled, moving MOS1 from GPIO4 to GPIO3 requires changing only INC_LIGHT1_PIN to 3 and moving that control wire. Rebuild and test both OFF levels, boot behavior and PWM before reconnecting the load. Do not edit configurePwm() argument lists or replace every numeric 4 in the project.

The compile check rejects active pin collisions and excludes USB pins, flash pins and strapping GPIO2/8/9. Photo AO is restricted to ADC1 GPIO0..4. GPIO18/19 serve native USB and are not spare RAM-saving choices. GPIO20/21 can conflict with UART wiring. See the [ESP32-C3 datasheet](https://documentation.espressif.com/esp32-c3_datasheet_en.html) and your board schematic; another C3 board can have a different exposed pin set.

Adding a sensor: give sampling a bounded duration, reuse the owning snapshot rather than sharing mutable buffers across tasks, add an explicit protocol field/version, validate it on NAS, and update tests and both wiring pages. Add raw fields to the Flash record only with a new storage schema/migration; sizeof(Record)==124 is intentional. Increasing page count also changes RAM metadata, filesystem use and NAS validation bounds, so it is not a single safe knob.

Static HTML/font data live in Flash. Flash cannot act as automatic swap space for Wi-Fi, BLE or task stacks. After editing web_ui.h run python tools/generate_web_assets.py. Run tests and the firmware build after every change; use the memory and samples info serial commands to inspect runtime allocations and filesystem capacity.

