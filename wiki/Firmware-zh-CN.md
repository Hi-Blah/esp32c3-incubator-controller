# 固件编译与刷写

安装 Arduino IDE 或 Arduino CLI，使用 Espressif 官方开发板索引：

https://espressif.github.io/arduino-esp32/package_esp32_index.json

选择 ESP32 开发板包 **3.3.12**、**ESP32C3 Dev Module**、**4 MB Flash**、**USB CDC On Boot 启用**、**Huge APP（3 MB No OTA / 1 MB SPIFFS）**。升级时将 **Erase All Flash 关闭**。标签名 spiffs 的数据分区由 LittleFS 挂载；本分区方案没有双应用 OTA 槽。

依赖版本固定在 firmware/dependencies.json：Adafruit SSD1306 2.5.17、GFX 1.12.6、BusIO 1.17.4、U8g2_for_Adafruit_GFX 1.8.0。将 third_party/IncubatorNimBLE 复制到 Arduino 库目录，或使用下方脚本。专用封装可避免误用电脑上另一份 NimBLE。当前 DHT11 为自定义 RMT 解码，不需要 Adafruit DHT 库。

编译前检查 module_config.h、board_config.h、web_entry.h 的 NAS 地址。.ino 名称必须与草图文件夹一致。CLI 操作如下，每个代码块是一条命令：

~~~sh
python tools/generate_web_assets.py
~~~

~~~sh
python tools/build_firmware.py --install
~~~

脚本安装固定依赖，将源码暂存到短路径编译，完成后显示产物目录，不会自动刷开发板。Windows 用户目录过长或包含特殊字符时，可加 --work C:/incubator-build；已有 CLI 配置可用 --config PATH 指定。

使用 IDE 时，打开 firmware/IncubatorController/IncubatorController.ino，先“验证”，再核对实际开发板及 COM 口后上传；保持 24 V 断开。上传后先查 status、diag、samples info，再确认已有程序和恢复设置。普通升级不更换分区方案。

4 MB Huge APP 分区表中：应用 app0 从 0x10000 开始、长 0x300000；文件系统从 0x310000 开始、长 0xE0000，即 896 KiB；NVS 位于 0x9000、长 0x5000，另有启动/OTA 元数据及 coredump。文件、元数据、写时复制都占文件系统空间；应用分区中未用到的空间不会自动成为历史记录空间。

参考：[Arduino CLI](https://docs.arduino.cc/arduino-cli/getting-started/)、[Espressif Arduino](https://github.com/espressif/arduino-esp32)。构建采用官方编译配方；优化和链接清理以实际构建输出为准，不默认声称开启 LTO。

