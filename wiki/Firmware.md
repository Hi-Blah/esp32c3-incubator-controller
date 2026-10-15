# Firmware build and upload

Install Arduino IDE or Arduino CLI and the official Espressif board index:

https://espressif.github.io/arduino-esp32/package_esp32_index.json

Use ESP32 core **3.3.12**, board **ESP32C3 Dev Module**, **4 MB Flash**, **USB CDC On Boot Enabled**, **Huge APP (3 MB No OTA / 1 MB SPIFFS)** and **Erase All Flash Disabled** for upgrades. The partition label spiffs is mounted as LittleFS. This selection does not provide OTA app slots.

Pinned external library versions are in firmware/dependencies.json: Adafruit SSD1306 2.5.17, GFX 1.12.6, BusIO 1.17.4, U8g2_for_Adafruit_GFX 1.8.0. Copy third_party/IncubatorNimBLE into the Arduino sketchbook libraries directory, or use the helper below. Its wrapper distinguishes it from an unrelated global NimBLE installation. The custom RMT DHT11 decoder does not require the Adafruit DHT library.

Before building, review module_config.h, board_config.h and the NAS origin in web_entry.h. The .ino filename must match its containing sketch folder. For CLI use, each block is one command:

~~~sh
python tools/generate_web_assets.py
~~~

~~~sh
python tools/build_firmware.py --install
~~~

The helper installs the pinned dependencies, stages source under a temporary work directory, compiles and reports the output path. It never uploads to a board. On Windows with a non-ASCII/very long user path, pass --work C:/incubator-build. An existing Arduino CLI configuration can be selected with --config PATH.

For Arduino IDE open firmware/IncubatorController/IncubatorController.ino, Verify, then Upload after checking the correct physical board and COM port. Keep 24 V disconnected. After upload verify status, diag and samples info before loading existing programs. Do not change the partition scheme during an ordinary upgrade.

The 4 MB Huge APP table reserves app0=0x300000 bytes at 0x10000; filesystem=0xE0000 bytes (896 KiB) at 0x310000; NVS=0x5000 at 0x9000, plus boot/OTA metadata and coredump. File data, metadata and copy-on-write need space inside that filesystem. Free space inside the application partition is not automatically usable for records.

Sources: [Arduino CLI](https://docs.arduino.cc/arduino-cli/getting-started/), [Espressif Arduino core](https://github.com/espressif/arduino-esp32). Compiler flags use the official core recipes; size optimization and section garbage collection should be verified in the build output rather than assuming LTO.

