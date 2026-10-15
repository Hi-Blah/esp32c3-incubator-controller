# Security / 安全报告

Use this system on a trusted network. MQTT on port 1883 and Basic-auth HTTP are not encrypted; use a properly configured TLS reverse proxy or VPN for access across untrusted networks. Do not expose the ESP32 fallback page or broker directly to the Internet.

For a vulnerability, use GitHub private vulnerability reporting if enabled, or a private contact supplied by the repository owner. Do not put secrets, exploitable credentials or live device addresses into a public issue. No private contact address is embedded in this repository.

如发现安全问题，优先使用仓库启用的 GitHub 私密漏洞报告，或所有者另行提供的私密联系方式。请勿将密码、可直接利用的凭据或真实设备地址写入公开 issue。

The light controller is not a safety-rated overcurrent protection system. Use suitable fuses, wiring, insulation and a load-rated power supply independently of software.

