# 针脚、模块与配置定制

公开版将接线集中在 firmware/IncubatorController/board_config.h。默认接线保持原方案；自定义接线需要重新实测，不能直接沿用默认硬件的验收结论。

| 想修改的内容 | 修改位置 | 实际使用处 |
|---|---|---|
| 两路 MOS 控制 | board_config.h 的 INC_LIGHT1_PIN / INC_LIGHT2_PIN | application.cpp 的 LIGHT_PIN[]、configurePwm(ch)、drive(ch,on)；ch 从 0 开始 |
| OLED I2C | INC_I2C_SDA_PIN / INC_I2C_SCL_PIN | applicationSetup() 中 Wire.begin(...,400000)，地址自动探测 0x3c/0x3d |
| DHT DATA | INC_DHT_PIN | bounded_dht.h 的 BoundedDht11::pin；换针脚不需要改 RMT 时序 |
| 光敏 AO | INC_PHOTO1_PIN / INC_PHOTO2_PIN | light_feedback.cpp 的 ADC 初始化和采样 |
| 按键 | INC_PAGE_BUTTON_PIN 和两路按键宏 | serviceButtons()，内部上拉 INPUT_PULLUP |
| 关闭模块 | module_config.h 的 INC_ENABLE_BLE / DHT / OLED / PHOTO | 编译时排除；PHOTO=0 时 GPIO0/1 恢复为两路按键 |
| 传感器 Flash 缓存 | INC_ENABLE_SAMPLE_JOURNAL、INC_SAMPLE_INTERVAL_MS、INC_SAMPLE_FAST_PHASE_MS、INC_SAMPLE_SLOW_PHASE_MS | sample_journal.cpp；默认开启；联网10000 ms；断联前三分钟1秒，3–15分钟3秒，此后10秒；阶段门限180000/900000 ms |
| 时区 | module_config.h 的 INC_TIMEZONE | POSIX时区；CST-8为UTC+8；每日定时按此解释 |
| NAS 网页地址 | web_entry.h 的 INC_NAS_WEB_ORIGIN | 开发板简易入口跳转；编译前改为自己的 NAS URL |
| Wi-Fi、MQTT | 开发板本地网页 | 保存到 NVS，不要把密码写进公开源码 |
| 程序、恢复、PWM | 每路网页或命令 | 已有持久配置，不需要改源码 |

例：所有默认模块开启时，把 MOS1 从 GPIO4 改到 GPIO3，只需将 INC_LIGHT1_PIN 改为 3，并将该控制线重新接到 GPIO3。重编译后先检查关闭电平、重启和 PWM，再接负载。不需要修改 configurePwm() 参数列表，也不能把工程里所有数字 4 批量替换。

编译检查会拒绝正在使用的针脚重复，并排除 USB、Flash 及 GPIO2/8/9 启动配置脚。光敏 AO 限 ADC1 GPIO0..4。GPIO18/19 用于原生 USB，不能随意占用；GPIO20/21 还需核对 UART 接线。参照[ESP32-C3 数据手册](https://documentation.espressif.com/esp32-c3_datasheet_en.html)及实际开发板原理图，其他 C3 板型的引出脚可能不同。

增加传感器时，应采用有时间上限的采样，由所属任务更新快照，避免任务间共享可变缓冲区；同步增加明确的协议字段和 NAS 校验、测试、双语接线说明。Flash Record 固定为 124 字节，修改字段必须升级存储格式或迁移。增加页数还会影响 RAM 元数据、文件系统空间和 NAS 容量校验，不能只改一个数字就视为完成。

静态网页和字体放在 Flash；Flash 不能自动替代 Wi-Fi、蓝牙和任务栈需要的 RAM。修改 web_ui.h 后执行 python tools/generate_web_assets.py。每次修改均需重新编译、测试，并用 memory、samples info 查看运行内存与文件系统容量。

