# Third-party notices

Project license selection does not replace these third-party licenses.

| Component | Version / use | License and source |
|---|---|---|
| IncubatorNimBLE / NimBLE-Arduino | 2.5.1; peripheral-only wrapper/profile | Apache-2.0 root license; nested NimBLE and TinyCrypt notices preserved under third_party/IncubatorNimBLE; upstream https://github.com/h2zero/NimBLE-Arduino |
| paho-mqtt | Vendored Python 2.1.0 | EPL-2.0 / EDL-1.0 alternatives and attribution in services/recorder/vendor/paho_mqtt-2.1.0.dist-info/licenses; upstream https://github.com/eclipse-paho/paho.mqtt.python |
| Arduino-ESP32 | Build dependency 3.3.12, not vendored | Upstream licenses apply; https://github.com/espressif/arduino-esp32 |
| Adafruit SSD1306, GFX, BusIO | External build dependencies | Their upstream license files apply; https://github.com/adafruit |
| U8g2_for_Adafruit_GFX | External font/display dependency | Preserve upstream font and source notices; https://github.com/olikraus/U8g2_for_Adafruit_GFX |
| Eclipse Mosquitto | External broker image | Upstream licensing: https://mosquitto.org |

NimBLE modifications: renamed library metadata and wrapper header; central/observer roles disabled and one peripheral connection selected in src/nimconfig.h. Original copyright statements are retained. Python paho files are included unmodified; project code explicitly selects the vendored 2.1.0 runtime.

第三方作者、版权和许可证文字属于必要署名，不作为需要删去的私人信息。

