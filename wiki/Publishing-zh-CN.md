# 配置备份、复制与版本选择

## 选择版本

从[版本下载目录](../archive/README.zh-CN.md)选择版本。Connected-2.2.2 与 NAS-Recorder-2.2.2c 为原型参考组合；2.2.3 新增 Flash 采样缓存，仍待实机验收。较早版本用于复现特定行为或研究实现。单独下载 ZIP 后，优先阅读包内说明，不把当前接线、模块设置或部署步骤直接套到旧版。

固件和记录器是两条版本线。固件回退不要求生产记录器同步回退。历史记录器使用独立数据目录，不能让旧数据库写入程序打开新版生产库。下载后可用 archive/catalog.json 中的 SHA256 校验包的完整性。

## 备份与恢复开发板

tools/backup 中的 PowerShell 工具读取实际分区表，再读两份 NVS 比较哈希。NVS 含 Wi-Fi/MQTT 凭据、逻辑身份、BLE 配对和程序状态，备份需妥善保存。保存工具的 -FullFlash 可同时备份应用及文件系统；仅 NVS 备份不包含传感器历史缓存。

先断开 24 V 与外部 GPIO，锁定输出、关闭 MQTT 和串口监视器，再进入 BOOT 下载模式。核对实际板子与 COM。Save-IncubatorConfig.ps1 使用 -Port，可通过 -ToolPath 指定工具，参数见 Get-Help。仅向兼容的 4 MB ESP32-C3 和分区布局恢复，并读回验证。恢复后首次启动只用 USB，因为保存的恢复策略可能自动打开输出。

一对一换板时令被替换板保持离线，保留逻辑 ID、ACL 和历史关联。复制为独立控制器时，在支持身份修改的固件上依次发送 disarm、mqtt off、device id new，再重启；开启 MQTT 前分配新的 NAS 设备编号和 ACL。更早的版本没有此命令，按硬件产生身份。恢复 NVS 不会改变硬件 MAC。

## 维护 GitHub Wiki

主仓库 wiki/ 中的双语文档可以直接阅读。GitHub 的 Wiki 功能使用独立的 .wiki.git 仓库；先在 Wiki 页面创建 Home，再克隆并用 tools/export_wiki.py 复制页面、改写链接，核对差异后单独提交推送。工具不会自动推送。功能可用性与可见性及套餐有关，参见[GitHub Wiki 文档](https://docs.github.com/en/communities/documenting-your-project-with-wikis/about-wikis)。
