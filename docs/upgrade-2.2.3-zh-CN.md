# 从现有稳定安装升级到2.2.3候选版

先升级NAS，再刷固件；Docker镜像、网络、密码、SMTP和records都保留，不回退现有安装。

1. 备份SQLite（backup API或先停止写入容器），备份现有compose和本地秘密文件到私人位置。
2. 解压NAS包到新目录；把recorder目录交给tools/upgrade_existing_compose.py作为source。生成并检查并排compose.local.2.2.3.yaml，再config --quiet和up -d。传统YAML需手工合并挂载，不覆盖现有文件。
3. 在现有broker ACL中补充设备写samples/读samples/ack、记录器读samples/写samples/ack，然后HUP重载。详情见wiki/Deployment-zh-CN.md。
4. 编译刷入firmware/IncubatorController，不擦除NVS、不变分区。先断24V并锁定；新日志分区未初始化时按首次安装说明操作，已有数据时不发送logs init。
5. samples info应显示容量2048，NAS面板能看缓存及CSV。短断联前3分钟1秒、3分钟后3秒、15分钟后10秒；额外保留关键状态和锚点。先做两分钟无负载测试，再做长断联和重启测试。
6. NAS直接校时显示来源NAS；NTP提供备用。无法定位的旧开机段保持unknown。断联邮件超过180秒才创建，界面/控制仍遵循原有新鲜度规则。

本候选版必须完成wiki/Validation-zh-CN.md中的实机验收后再标稳定。历史补传不会回填HA原生历史。
