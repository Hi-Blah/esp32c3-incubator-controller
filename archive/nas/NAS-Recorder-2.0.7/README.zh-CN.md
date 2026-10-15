# NAS-Recorder-2.0.7

[English](README.md)

环境、事件和进度记录，以及早期 NAS 循环恢复。 历史版本，使用前需核对兼容性。

这是累计完整源码包，不要求先找到未注明的前置目录。保留历史配置模板，另提供 compose.archive.yaml 作为独立测试的便携示例。历史运行程序不保证兼容所有新版固件或生产数据库。

## 单独下载后的安装步骤

前置条件：Docker Compose v2、可以访问的 MQTT broker，以及已配置的设备与记录器账号；Home Assistant 可选。包内 mqtt/config/acl.archive.example 提供按设备限定的权限示例，替换示例编号与用户名、合并进 broker ACL 后重载。此包提供记录器，不附带已配置好的 broker。

1. 解压到新目录并进入 recorder；先读 compose.archive.yaml 及 recorder.py 开头读取的环境变量。有 .env.example 时据此创建 .env，填写实际 MQTT 地址、设备编号和记录器账号。192.0.2.10 与 a1b2c3 均为示例，必须替换。
2. 本地创建 logger_password.txt。包含 notifier.py 的版本还需按其读取字段创建 smtp.json、smtp_password.txt；存在 smtp.example.json 时以其为模板。文件只授予服务所需的读取权限，密码不写入命令参数。
3. 创建 records 并令选定的 PUID/PGID 可写，默认 1000:1000。vendor 内含 paho-mqtt 2.1.0 及许可证。便携示例使用 Python 3.13-slim，不需 pip 安装；RECORDER_IMAGE 可指向兼容的已有本地镜像。
4. 下面每个代码块是一条命令，分别执行：

~~~sh
docker compose -f compose.archive.yaml config --quiet
~~~

~~~sh
docker compose -f compose.archive.yaml up -d
~~~

查看 recorder 日志。存在仪表盘时默认仅绑定 localhost:8790；需要局域网访问时明确设置绑定地址或可信反向代理。
5. 为实际设备编号配置 broker ACL。若包中有 mqtt/config/acl，可作起点并核对 recorder.py 的订阅/发布主题；各版的采样、状态、事件、进度与恢复权限不同。NAS 控制、分析心跳和配置主题也需相应权限；2.2.3 新增 samples 与 samples/ack。

## 已有系统升级或版本对照

先备份数据库、Compose、环境与凭据文件。保留现有镜像、用户、网络、挂载、密码和 records，不用示例覆盖现役 Compose。根据本版模块清单补挂载后再重建对应服务。旧版记录器仅使用独立测试数据库，不能直接打开新版生产库；固件回退不要求 Docker 同步回退。

## 配套和验收

先使用同阶段固件：2.0.7/2.0.8 对应早期恢复协议，2.1.0 增加光反馈/通知，2.2.x 支持 NAS 分析与控制。固件 2.2.2 的累计参考组合为 NAS 2.2.2c。上线前验证状态新鲜度、采样年龄、CSV、恢复回复及邮件实际投递；仅 SMTP 登录成功不代表邮件已发送。各版已知限制见归档目录；项目许可证暂未指定。
