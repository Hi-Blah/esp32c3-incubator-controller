# Contributing / 参与开发

Project-original material is licensed under Apache-2.0. Unless explicitly stated otherwise, contributions intentionally submitted for inclusion are provided under the same license. Preserve third-party copyright and license notices.

项目原创内容采用Apache-2.0；除非另有明确声明，为纳入项目而主动提交的贡献按相同许可证提供。保留第三方版权和许可声明。

Report the firmware and NAS versions, board/module types, build options, reproduction steps and a minimal sanitized status/diagnostic excerpt. Remove Wi-Fi/MQTT/SMTP credentials, complete flash dumps, private IPs, email addresses and experimental subject data.

Keep firmware control and acquisition bounded. Put model fitting, historical analysis and notifications on NAS. Avoid another full JSON snapshot buffer or task stack when an existing owner can be reused. Document every pin change in board_config.h and the bilingual wiring pages.

Run backend tests, host storage tests, firmware compilation, web-asset consistency and public-file checks before a change. Record hardware checks separately from unit tests. Update both wiki languages when behavior changes.

提交问题请附固件/NAS 版本、模块型号、编译选项、复现步骤和已脱敏的诊断片段。不要上传密码、完整 Flash、私人地址和实验数据。修改默认针脚时同步更新中英文接线说明；算法测试通过不等于完成实机验收。

