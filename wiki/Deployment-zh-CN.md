# NAS Docker 首次部署与升级

NAS 需有 Docker Compose v2；准备脚本需 Python 3.12 或更新版本。运行镜像已包含 paho-mqtt 2.1.0，不再临时联网 pip 安装；但 Python、Mosquitto 基础镜像仍需本地已有或能下载。镜像标签可能变化，严格复现时记录解析后的 digest。

### 首次安装

把整个仓库复制/克隆到 NAS 文件夹，在该目录执行。先从开发板 device info 获取编号；a1b2c3 是虚构示例，务必换成自己的六位编号。

~~~sh
sudo python3 tools/setup_runtime.py --device a1b2c3 --uid 1000 --gid 1000
~~~

UID/GID 与 NAS 实际用户不同则修改。脚本分别询问三种 MQTT 账号密码，生成 ACL、配置及本地私有文件，并调用官方 mosquitto_passwd 转换哈希。已有 .env 或 runtime 时拒绝覆盖，密码不放进命令参数。已哈希文件不能再次执行转换。

~~~sh
sudo docker compose config --quiet
~~~

~~~sh
sudo docker compose up -d --build
~~~

~~~sh
sudo docker compose logs --tail=40 recorder
~~~

应看到 Recorder connected，数据库位于 /records。开发板填写 NAS 主机、1883 端口及 incubator_device 密码；HA MQTT 集成使用 incubator_ha。NAS 主网页为 http://自己的NAS:8790/device/ ，用户名 incubator，密码与本地 runtime/secrets/logger_password.txt 一致；不要把文件发给别人。

实际数据保存在 runtime/records/incubator.sqlite3。HTTP 网页与未加密 MQTT 仅限可信网络。路由、DNS、VPN 的连通性问题与 MQTT 账号分开检查。

发件功能需单独填写 runtime/secrets/smtp.json，并在本地写入 smtp_password.txt。启用邮件容器：

~~~sh
sudo docker compose --profile mail up -d
~~~

~~~sh
sudo docker compose exec mail python3 -S notifier.py --check
~~~

第二条仅检查配置和状态，不发邮件。见[校准与邮件](Calibration-zh-CN.md)中的实际投递测试。

### 已有 NAS 升级，不回退 Docker 配置

保留原镜像、网络、用户、.env、密码文件、smtp.json、records。SQLite 应使用 backup API 备份，或先停写入服务再复制；只复制正在使用的主文件可能遗漏 WAL 中的数据。

将本次 services/recorder 累计源码放到新目录。旧 compose.local.yaml 为 JSON 格式时，可生成一份并排配置：

~~~sh
python3 tools/upgrade_existing_compose.py --file /自己的旧目录/compose.local.yaml --source /自己的新目录/services/recorder
~~~

检查生成的 compose.local.2.2.3.yaml，再用该文件执行 config --quiet、up -d。传统 YAML 不自动改写：手动加入 /app/sample_journal.py 和累计运行模块/vendor 的挂载，保留密码、记录、网络、镜像；mail 服务也更新累计 notifier.py。

在 broker 原有对应用户块中增加（替换设备编号）：

~~~text
# 放在 user incubator_device 块内
 topic write incubator/a1b2c3/samples
 topic read incubator/a1b2c3/samples/ack
# 放在 user incubator_logger 块内
 topic read incubator/a1b2c3/samples
 topic write incubator/a1b2c3/samples/ack
~~~

这些只补充新主题权限，不替换现有 state/config/analysis/recovery 权限。核对 ACL 后再 HUP 重载 broker。先升级 NAS，再启用新固件缓存；旧固件仍可接入。数据库为新增表，不清除已有历史。

