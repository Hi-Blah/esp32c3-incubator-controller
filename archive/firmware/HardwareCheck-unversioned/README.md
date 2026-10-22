# HardwareCheck-unversioned

[简体中文](README.zh-CN.md)

## Purpose and status

Hardware/local control prototype. Unversioned early example. This is a source archive for comparison and reproduction, not the current default installation.

## Hardware

4 MB ESP32-C3 board, 3.3 V-compatible SSD1306 I2C OLED, DHT11, one or two suitable DC MOS switch modules, LED loads and a correctly rated 24 V supply. Buttons are optional. Versions supporting optical feedback use two 3.3 V LDR modules with analog outputs; earlier versions use those pins for buttons. See the included [power wiring diagram](../../../assets/wiring-power.svg); use the version-specific signal pin definitions below. No version in this archive provides INA226 current sensing or hardware short-circuit protection.

## Build this version

Open HardwareCheck/HardwareCheck.ino. Install the exact dependencies below using the Arduino Library Manager, except the custom BLE library. Verify before uploading. The archive has not been fully rebuilt or hardware-requalified as part of this archive publication unless stated in manifest.json.

Arduino-ESP32 3.3.12; ESP32C3 Dev Module; USB CDC On Boot Enabled; 4 MB Flash; Huge APP; 115200 baud.
BLE: none.

- Adafruit SSD1306 2.5.17
- Adafruit GFX 1.12.6
- Adafruit BusIO 1.17.4
- DHT sensor library 1.4.7
- Adafruit Unified Sensor 1.1.15

For IncubatorNimBLE, download the matching dependency archive from the repository archive/dependencies directory and install its ZIP. NimBLE-Arduino 2.5.1 is the upstream library, distinct from the custom IncubatorNimBLE profile. Built-in BLE requires no additional BLE library.

## Wiring and configuration

Default control pins are MOS1=GPIO4, MOS2=GPIO7, OLED SDA=GPIO6/SCL=GPIO5, DHT11 DATA=GPIO21. Use 3.3 V sensor outputs and common logic ground. Keep the 24 V circuit disconnected for first checks. GPIO18/19 are USB; do not use them for sensors. This version's own pin definitions follow; do not copy a later board_config.h into an older sketch.

    constexpr int PIN_SDA = 6;
    constexpr int PIN_SCL = 5;
    constexpr int PIN_DHT = 21; // UART0 TX reused; use USB CDC Serial, not Serial0.
    constexpr int PIN_LIGHT1 = 4;
    constexpr int PIN_LIGHT2 = 7;
    DHT dht(PIN_DHT, DHT11);
    if (Wire.begin(PIN_SDA, PIN_SCL, 100000)) {

This version predates optical ADC acquisition: GPIO0/1/10 may be buttons. Do not connect later optical modules to GPIO0/1 on this wiring.

For custom pins, edit the declarations shown above in application.cpp and, when present, module_config.h/light_feedback.cpp. Version 2.2.3 uses board_config.h instead. Search this extracted source for every use of a changed pin and verify no collision. Where present, PWM uses LEDC; do not assume all historical releases support 20 kHz or changing PWM during a running program. Consult the USB help output for the commands this version actually supports.

The default local schedule/display timezone is UTC+8. Version 2.2.3 centralizes it as INC_TIMEZONE in module_config.h; older firmware and recorder timestamp assumptions must be checked together before changing timezone.

## First boot and network

Use a separate test board or back up NVS/filesystem before changing versions. Do not erase all Flash or change partitions on a running installation. After flashing, keep loads disconnected, open USB serial at 115200 and query help/status where supported. Newer releases accept disarm; do not assume it exists in an early example. Configure Wi-Fi through this version's local AP, BLE or USB interface and use the IP it reports. Stored credentials and device identity come from NVS; examples in source are not working connection settings.

## NAS / Home Assistant compatibility

Suggested companion: none. MQTT-enabled releases require an explicitly configured broker and matching device/topic ACL. On older releases the device ID is derived from hardware; do not assume the later device id command exists. Import no current NAS database into an old recorder. A firmware comparison does not require downgrading a production NAS. New deployments should use the current project or the 2.2.2 / NAS-2.2.2c reference pair.

## Validation and license

Check boot/version, sensor validity, each output independently, PWM, reconnect and the recovery functions present in this version. Known issue: Unversioned early example. Project-original code is licensed under Apache-2.0; third-party licenses retain their terms. 
