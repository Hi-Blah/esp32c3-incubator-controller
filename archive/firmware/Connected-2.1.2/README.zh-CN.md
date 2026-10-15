# Connected-2.1.2

[English](README.md)

## 用途与状态

热点及受限 HTTP 传输诊断。 有 MQTT 内存保护停止和网页超时报告。 本包用于版本对照与复现，不是当前默认安装版本。

## 硬件准备

4 MB ESP32-C3、适配 3.3 V 的 SSD1306 I2C OLED、DHT11、一路或两路合适的直流 MOS 开关模块、LED 负载及容量匹配的 24 V 电源；按键可选。带光反馈的版本使用两个 3.3 V 光敏电阻 AO 模块，更早版本把相关针脚用作按键。参见包内[电源接线图](../../../assets/wiring-power.svg)，信号接线以本版定义为准。这些版本均未提供 INA226 电流采集或硬件短路保护。

## 单独下载后如何编译

打开 IncubatorConnected/IncubatorConnected.ino，按下列版本安装依赖，先验证再上传。除 manifest.json 明确列出的项目，本次归档没有重新完成整机编译和实机验收。

Arduino-ESP32 3.3.12; ESP32C3 Dev Module; USB CDC On Boot Enabled; 4 MB Flash; Huge APP; 115200 baud.
BLE: Arduino-ESP32 built-in BLE.

- Adafruit SSD1306 2.5.17
- Adafruit GFX 1.12.6
- Adafruit BusIO 1.17.4
- U8g2_for_Adafruit_GFX 1.8.0

IncubatorNimBLE 请从仓库 archive/dependencies 下载配套 ZIP 后安装。NimBLE-Arduino 2.5.1 为上游库，与定制 IncubatorNimBLE 配置不同。内置 BLE 不额外安装同名第三方库。

## 本版本接线与自定义

默认 MOS1=GPIO4、MOS2=GPIO7、OLED SDA=GPIO6/SCL=GPIO5、DHT11 DATA=GPIO21。传感器信号必须适配 3.3 V；首次检查断开 24 V，逻辑地按接线图共地。GPIO18/19 用于 USB。以下定义直接来自本版本源码，不可向旧版直接复制新版 board_config.h：

    constexpr uint8_t LIGHT_PIN[2] = {4,7};
    constexpr uint8_t BUTTON_PIN[1]={INC_PAGE_BUTTON_PIN};
    constexpr uint8_t BUTTON_PIN[3]={0,1,INC_PAGE_BUTTON_PIN};
    Wire.begin(6,5,400000);Wire.setTimeOut(30);
    constexpr uint8_t pins[2]={INC_PHOTO1_PIN,INC_PHOTO2_PIN};
    #define INC_PHOTO1_PIN 0
    #define INC_PHOTO2_PIN 1
    #define INC_PAGE_BUTTON_PIN 10

光采集启用时 GPIO0/1 为模拟输入，仅 GPIO10 保留为翻页按钮；接线前检查 module_config.h。

修改 application.cpp 中上述定义；存在 module_config.h/light_feedback.cpp 时一并核对。2.2.3 改为 board_config.h 集中管理。修改后搜索该包全部引用并检查冲突。PWM 使用 LEDC；不同版本不一定都支持 20 kHz 或运行中改 PWM，以本版 USB help 返回的命令为准。

默认本地定时和显示时区为 UTC+8。2.2.3 使用 module_config.h 中的 INC_TIMEZONE；修改更早版本时区时，需同时核对固件与记录器的时间解释。

## 首次启动与联网

优先使用独立测试板；升级或降级前备份 NVS/文件系统，不对现役设备直接擦除全部 Flash 或改分区。刷入后保持负载断开，以 115200 打开 USB 串口；支持时查询 help/status，新版可用 disarm，早期示例不保证存在。通过该版本自身的热点、BLE 或 USB 配网，以它实际显示的 IP 为准。源码示例不是可直接使用的网络配置，凭据和逻辑身份来自 NVS。

## NAS / HA 配套

建议配套：NAS-Recorder-2.1.0。启用 MQTT 的版本需填写 broker、账号，并为实际设备编号配置相应主题 ACL；旧版按硬件产生编号，不保证具备后来的 device id 命令。旧版记录器只用于独立测试目录，不可打开新版生产数据库。仅做固件对照时，不需要把正在使用的 NAS 降级。新部署应优先选择当前版本或 2.2.2 / NAS-2.2.2c 参考组合。

## 验证与许可

逐项确认版本、传感器有效性、单路开关、PWM、重连，以及本版本已提供的恢复功能。已知问题：有 MQTT 内存保护停止和网页超时报告。 项目许可证暂未指定；第三方库遵循各自许可。
