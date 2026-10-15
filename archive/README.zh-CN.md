# 版本下载目录

[English](README.md)

每份 ZIP 都包含独立的中英文安装说明、准确的草图名称或 NAS 入口、依赖版本、接线和配置、兼容边界及已知问题。选择版本后先阅读该包自己的说明。

- 原型参考组合：Connected-2.2.2 与累计 NAS-Recorder-2.2.2c。
- 候选组合：Connected-2.2.3 与 NAS-Recorder-2.2.3；Flash 缓存仍需实机验收。
- 更早版本用于复现历史行为，可能保留已知故障。提供归档不等于认定稳定，也不等于重新完成全部编译验证。
- HardwareCheck、Basic、早期 Wi-Fi 示例没有内置语义版本号，因此保留未编号标记。

从表格直接下载 ZIP；若进入 GitHub 文件页面，点击 Download raw file。解压到新目录，从包内 README 开始操作。可按 [catalog.json](catalog.json) 或 [SHA256SUMS.txt](SHA256SUMS.txt) 验证哈希。不可把旧版草图与新版头文件混用，旧记录器也不能打开新版生产数据库。NAS 的 a/b/c 归档是累计完整源码，不需要另找前置补丁包。

配置示例使用文档地址和示例设备编号，实际连接信息需通过所选版本支持的接口设置。运行行为与针脚布局按该版本保留；压缩网页由相邻源码重新生成，身份测试使用合成向量。第三方依赖附带自身许可证，项目级许可[暂未指定](../LICENSE-STATUS.md)。

## 固件

| 版本 | 状态 | 说明 | 下载 |
|---|---|---|---|
| Advanced-1 | Historical / 历史 | [README](firmware/Advanced-1/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Advanced-1/Advanced-1-Source.zip) |
| Advanced-1.1 | Historical / 历史 | [README](firmware/Advanced-1.1/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Advanced-1.1/Advanced-1.1-Source.zip) |
| Connected-2.0 | Historical / 历史 | [README](firmware/Connected-2.0/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0/Connected-2.0-Source.zip) |
| Connected-2.0.1 | Historical / 历史 | [README](firmware/Connected-2.0.1/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.1/Connected-2.0.1-Source.zip) |
| Connected-2.0.2 | Historical / 历史 | [README](firmware/Connected-2.0.2/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.2/Connected-2.0.2-Source.zip) |
| Connected-2.0.3 | Historical / 历史 | [README](firmware/Connected-2.0.3/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.3/Connected-2.0.3-Source.zip) |
| Connected-2.0.4 | Historical / 历史 | [README](firmware/Connected-2.0.4/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.4/Connected-2.0.4-Source.zip) |
| Connected-2.0.5 | Historical / 历史 | [README](firmware/Connected-2.0.5/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.5/Connected-2.0.5-Source.zip) |
| Connected-2.0.6 | Historical / 历史 | [README](firmware/Connected-2.0.6/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.6/Connected-2.0.6-Source.zip) |
| Connected-2.0.7 | Historical / 历史 | [README](firmware/Connected-2.0.7/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.7/Connected-2.0.7-Source.zip) |
| Connected-2.0.8 | Historical / 历史 | [README](firmware/Connected-2.0.8/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.8/Connected-2.0.8-Source.zip) |
| Connected-2.0.9 | Historical / 历史 | [README](firmware/Connected-2.0.9/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.9/Connected-2.0.9-Source.zip) |
| Connected-2.0.10 | Historical / 历史 | [README](firmware/Connected-2.0.10/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.10/Connected-2.0.10-Source.zip) |
| Connected-2.1.0 | Historical / 历史 | [README](firmware/Connected-2.1.0/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.0/Connected-2.1.0-Source.zip) |
| Connected-2.1.1 | Historical / 历史 | [README](firmware/Connected-2.1.1/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.1/Connected-2.1.1-Source.zip) |
| Connected-2.1.2 | Historical / 历史 | [README](firmware/Connected-2.1.2/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.2/Connected-2.1.2-Source.zip) |
| Connected-2.1.2-WiFiDiag | Historical / 历史 | [README](firmware/Connected-2.1.2-WiFiDiag/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.2-WiFiDiag/Connected-2.1.2-WiFiDiag-Source.zip) |
| Connected-2.2.0 | Historical / 历史 | [README](firmware/Connected-2.2.0/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.0/Connected-2.2.0-Source.zip) |
| Connected-2.2.1 | Historical / 历史 | [README](firmware/Connected-2.2.1/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.1/Connected-2.2.1-Source.zip) |
| Connected-2.2.2 | Reference prototype / 原型参考 | [README](firmware/Connected-2.2.2/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.2/Connected-2.2.2-Source.zip) |
| WiFi-unversioned | Historical / 历史 | [README](firmware/WiFi-unversioned/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/WiFi-unversioned/WiFi-unversioned-Source.zip) |
| HardwareCheck-unversioned | Historical / 历史 | [README](firmware/HardwareCheck-unversioned/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/HardwareCheck-unversioned/HardwareCheck-unversioned-Source.zip) |
| IncubatorBasic-unversioned | Historical / 历史 | [README](firmware/IncubatorBasic-unversioned/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/IncubatorBasic-unversioned/IncubatorBasic-unversioned-Source.zip) |
| IncubatorWiFiNtpRetry-unversioned | Historical / 历史 | [README](firmware/IncubatorWiFiNtpRetry-unversioned/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/IncubatorWiFiNtpRetry-unversioned/IncubatorWiFiNtpRetry-unversioned-Source.zip) |
| Connected-2.2.3 | Candidate / 候选 | [README](firmware/Connected-2.2.3/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.3/Connected-2.2.3-Source.zip) |

## NAS

| Version / 版本 | Status / 状态 | Guide / 说明 | Download / 下载 |
|---|---|---|---|
| NAS-Recorder-2.0.7 | Historical / 历史 | [README](nas/NAS-Recorder-2.0.7/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.0.7/NAS-Recorder-2.0.7-Source.zip) |
| NAS-Recorder-2.0.8 | Historical / 历史 | [README](nas/NAS-Recorder-2.0.8/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.0.8/NAS-Recorder-2.0.8-Source.zip) |
| NAS-Recorder-2.1.0-local | Historical / 历史 | [README](nas/NAS-Recorder-2.1.0-local/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.1.0-local/NAS-Recorder-2.1.0-local-Source.zip) |
| NAS-Recorder-2.1.0 | Historical / 历史 | [README](nas/NAS-Recorder-2.1.0/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.1.0/NAS-Recorder-2.1.0-Source.zip) |
| NAS-Recorder-2.2.0 | Historical / 历史 | [README](nas/NAS-Recorder-2.2.0/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.0/NAS-Recorder-2.2.0-Source.zip) |
| NAS-Recorder-2.2.2 | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2/NAS-Recorder-2.2.2-Source.zip) |
| NAS-Recorder-2.2.2a | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2a/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2a/NAS-Recorder-2.2.2a-Source.zip) |
| NAS-Recorder-2.2.2b | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2b/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2b/NAS-Recorder-2.2.2b-Source.zip) |
| NAS-Recorder-2.2.2c | Reference prototype / 原型参考 | [README](nas/NAS-Recorder-2.2.2c/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2c/NAS-Recorder-2.2.2c-Source.zip) |
| NAS-Recorder-2.2.3 | Candidate / 候选 | [README](nas/NAS-Recorder-2.2.3/README.zh-CN.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.3/NAS-Recorder-2.2.3-Source.zip) |

## Dependencies / 配套依赖

- [IncubatorNimBLE 2.5.1](dependencies/README.md): custom profile for 2.2.2 and later / 2.2.2 起使用的定制配置。
- Upstream NimBLE-Arduino 2.5.1 for 2.2.0/2.2.1 is available through Arduino Library Manager / 2.2.0、2.2.1 的上游库可在 Arduino 库管理器安装。
- Earlier BLE implementations use Arduino-ESP32 built-in BLE / 更早版本使用核心内置 BLE。
