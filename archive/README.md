# Version downloads

[简体中文](README.zh-CN.md)

Each ZIP is a source package with its own English/Chinese installation guide, exact sketch name or NAS entry point, dependency versions, wiring/configuration notes, compatibility boundaries and known limitations. Open the guide for the selected version before building it.

- Reference pair: Connected-2.2.2 with cumulative NAS-Recorder-2.2.2c.
- Candidate: Connected-2.2.3 with NAS-Recorder-2.2.3; Flash journaling still requires hardware acceptance.
- Earlier versions reproduce historical behavior and can retain known faults. Archive publication is not a stability endorsement or a new full-build certification.
- HardwareCheck, Basic and Wi-Fi examples had no embedded semantic version; their names preserve that fact.

Download the ZIP directly from the table. If GitHub opens a file page, choose Download raw file. Extract to a new directory and start with the README inside that ZIP. Check SHA256 against [catalog.json](catalog.json) or [SHA256SUMS.txt](SHA256SUMS.txt). Do not mix an old sketch with current headers or point an old recorder at a current production database. The a/b/c NAS archives are cumulative complete source sets; separate prerequisite patch packages are not required.

Configuration examples use documentation endpoints and an example device ID. Configure real endpoints and credentials through the selected version's supported interface. Runtime behavior and pin layout remain version-specific. Historical compressed UI assets have been regenerated from their adjacent source; identity-test vectors are synthetic. Third-party licenses accompany the dependencies. Project-wide licensing is [not yet assigned](../LICENSE-STATUS.md).

## Firmware

| Version | Status | Guide | Download |
|---|---|---|---|
| Advanced-1 | Historical / 历史 | [README](firmware/Advanced-1/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Advanced-1/Advanced-1-Source.zip) |
| Advanced-1.1 | Historical / 历史 | [README](firmware/Advanced-1.1/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Advanced-1.1/Advanced-1.1-Source.zip) |
| Connected-2.0 | Historical / 历史 | [README](firmware/Connected-2.0/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0/Connected-2.0-Source.zip) |
| Connected-2.0.1 | Historical / 历史 | [README](firmware/Connected-2.0.1/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.1/Connected-2.0.1-Source.zip) |
| Connected-2.0.2 | Historical / 历史 | [README](firmware/Connected-2.0.2/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.2/Connected-2.0.2-Source.zip) |
| Connected-2.0.3 | Historical / 历史 | [README](firmware/Connected-2.0.3/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.3/Connected-2.0.3-Source.zip) |
| Connected-2.0.4 | Historical / 历史 | [README](firmware/Connected-2.0.4/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.4/Connected-2.0.4-Source.zip) |
| Connected-2.0.5 | Historical / 历史 | [README](firmware/Connected-2.0.5/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.5/Connected-2.0.5-Source.zip) |
| Connected-2.0.6 | Historical / 历史 | [README](firmware/Connected-2.0.6/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.6/Connected-2.0.6-Source.zip) |
| Connected-2.0.7 | Historical / 历史 | [README](firmware/Connected-2.0.7/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.7/Connected-2.0.7-Source.zip) |
| Connected-2.0.8 | Historical / 历史 | [README](firmware/Connected-2.0.8/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.8/Connected-2.0.8-Source.zip) |
| Connected-2.0.9 | Historical / 历史 | [README](firmware/Connected-2.0.9/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.9/Connected-2.0.9-Source.zip) |
| Connected-2.0.10 | Historical / 历史 | [README](firmware/Connected-2.0.10/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.0.10/Connected-2.0.10-Source.zip) |
| Connected-2.1.0 | Historical / 历史 | [README](firmware/Connected-2.1.0/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.0/Connected-2.1.0-Source.zip) |
| Connected-2.1.1 | Historical / 历史 | [README](firmware/Connected-2.1.1/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.1/Connected-2.1.1-Source.zip) |
| Connected-2.1.2 | Historical / 历史 | [README](firmware/Connected-2.1.2/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.2/Connected-2.1.2-Source.zip) |
| Connected-2.1.2-WiFiDiag | Historical / 历史 | [README](firmware/Connected-2.1.2-WiFiDiag/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.1.2-WiFiDiag/Connected-2.1.2-WiFiDiag-Source.zip) |
| Connected-2.2.0 | Historical / 历史 | [README](firmware/Connected-2.2.0/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.0/Connected-2.2.0-Source.zip) |
| Connected-2.2.1 | Historical / 历史 | [README](firmware/Connected-2.2.1/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.1/Connected-2.2.1-Source.zip) |
| Connected-2.2.2 | Reference prototype / 原型参考 | [README](firmware/Connected-2.2.2/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.2/Connected-2.2.2-Source.zip) |
| WiFi-unversioned | Historical / 历史 | [README](firmware/WiFi-unversioned/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/WiFi-unversioned/WiFi-unversioned-Source.zip) |
| HardwareCheck-unversioned | Historical / 历史 | [README](firmware/HardwareCheck-unversioned/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/HardwareCheck-unversioned/HardwareCheck-unversioned-Source.zip) |
| IncubatorBasic-unversioned | Historical / 历史 | [README](firmware/IncubatorBasic-unversioned/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/IncubatorBasic-unversioned/IncubatorBasic-unversioned-Source.zip) |
| IncubatorWiFiNtpRetry-unversioned | Historical / 历史 | [README](firmware/IncubatorWiFiNtpRetry-unversioned/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/IncubatorWiFiNtpRetry-unversioned/IncubatorWiFiNtpRetry-unversioned-Source.zip) |
| Connected-2.2.3 | Candidate / 候选 | [README](firmware/Connected-2.2.3/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/firmware/Connected-2.2.3/Connected-2.2.3-Source.zip) |

## NAS

| Version / 版本 | Status / 状态 | Guide / 说明 | Download / 下载 |
|---|---|---|---|
| NAS-Recorder-2.0.7 | Historical / 历史 | [README](nas/NAS-Recorder-2.0.7/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.0.7/NAS-Recorder-2.0.7-Source.zip) |
| NAS-Recorder-2.0.8 | Historical / 历史 | [README](nas/NAS-Recorder-2.0.8/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.0.8/NAS-Recorder-2.0.8-Source.zip) |
| NAS-Recorder-2.1.0-local | Historical / 历史 | [README](nas/NAS-Recorder-2.1.0-local/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.1.0-local/NAS-Recorder-2.1.0-local-Source.zip) |
| NAS-Recorder-2.1.0 | Historical / 历史 | [README](nas/NAS-Recorder-2.1.0/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.1.0/NAS-Recorder-2.1.0-Source.zip) |
| NAS-Recorder-2.2.0 | Historical / 历史 | [README](nas/NAS-Recorder-2.2.0/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.0/NAS-Recorder-2.2.0-Source.zip) |
| NAS-Recorder-2.2.2 | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2/NAS-Recorder-2.2.2-Source.zip) |
| NAS-Recorder-2.2.2a | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2a/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2a/NAS-Recorder-2.2.2a-Source.zip) |
| NAS-Recorder-2.2.2b | Historical / 历史 | [README](nas/NAS-Recorder-2.2.2b/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2b/NAS-Recorder-2.2.2b-Source.zip) |
| NAS-Recorder-2.2.2c | Reference prototype / 原型参考 | [README](nas/NAS-Recorder-2.2.2c/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.2c/NAS-Recorder-2.2.2c-Source.zip) |
| NAS-Recorder-2.2.3 | Candidate / 候选 | [README](nas/NAS-Recorder-2.2.3/README.md) | [ZIP](https://github.com/Hi-Blah/esp32c3-incubator-controller/raw/refs/heads/main/archive/nas/NAS-Recorder-2.2.3/NAS-Recorder-2.2.3-Source.zip) |

## Dependencies / 配套依赖

- [IncubatorNimBLE 2.5.1](dependencies/README.md): custom profile for 2.2.2 and later / 2.2.2 起使用的定制配置。
- Upstream NimBLE-Arduino 2.5.1 for 2.2.0/2.2.1 is available through Arduino Library Manager / 2.2.0、2.2.1 的上游库可在 Arduino 库管理器安装。
- Earlier BLE implementations use Arduino-ESP32 built-in BLE / 更早版本使用核心内置 BLE。
