# 快速开始

1. 准备 4 MB Flash 的 ESP32-C3 SuperMini、USB 数据线、[清单中的模块](Hardware-zh-CN.md)及可信的 2.4 GHz Wi-Fi。
2. 首次测试断开全部 24 V 负载。先将 firmware/IncubatorController/web_entry.h 的 NAS 入口改为自己的地址；仓库示例地址不能直接用于联网。
3. 按[编译设置](Firmware-zh-CN.md)上传。升级已有板时关闭“上传前擦除全部 Flash”。
4. 以 115200 打开 USB 串口。分别发送 status、diag、device info、samples info。确认 logic=PASS、nvs=1、两路关闭。logs init 仅用于首次文件系统初始化；挂载失败时它可能格式化该分区，不能当成清理积压的命令。
5. 通过 wifi key 在本地查看随机生成的网页/热点密码。连接 Incubator-… 热点，打开 http://192.168.4.1/local 配网；之后让浏览器回到同一局域网，使用串口或 OLED 显示的设备 IP。不要将密码输出上传到 issue。
6. 按[部署说明](Deployment-zh-CN.md)使用本机六位设备编号创建 NAS 配套。锁定两路后，在本地网页填写 incubator_device 的 MQTT 账号密码。
7. 在 HA 和 NAS /device/ 页确认当前版本和正在增长的运行时间。仅收到 retained 的旧状态不能证明当前连接正常。
8. 先不接负载测试输出，再接一路带保险的灯源，确认极性；验证每日/循环与重启行为后再接两路。
9. 断电接入光敏模块，先看 ADC 是否随光变化，再做暗/满亮基准校准和 NAS 邮件设置。
10. 完成[断联补传验收](Validation-zh-CN.md)。积压时不要擦除 Flash 或重新初始化文件系统。

