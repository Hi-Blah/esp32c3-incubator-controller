# 私有备份与首次上传 GitHub

### 备份与更换开发板

tools/backup 中的 PowerShell 工具读取实际分区表，再读两份 NVS 比较哈希。NVS 含密码、身份、配对和程序状态，备份必须私下保存。保存脚本的 -FullFlash 还可读完整固件/文件系统；仅 NVS 备份不包含本次传感器历史缓存。

先断开 24 V 和外部 GPIO，锁定输出、关闭 MQTT、关闭串口监视器，再进 BOOT 下载模式。核对实际板子和当前 COM。Save-IncubatorConfig.ps1 用 -Port，可通过 -ToolPath 指定工具；脚本参数可用 Get-Help 查看。恢复要求兼容的 4 MB ESP32-C3 固件/分区，并读回校验。恢复后第一次启动只用 USB，因为保存的恢复授权可能自动开灯；不要把整片 Flash 随意写入不同板型。

一对一换板时保持旧板离线，保留逻辑 ID、ACL 和历史关联。复制为另一只独立开关时，恢复后依次发送 disarm、mqtt off、device id new，再重启；启用 MQTT 前分配新的 NAS 设备编号和 ACL。恢复 NVS 不会改写硬件 MAC。

### 首次上传仓库

从本次审核的干净目录开始，不把整个私人开发文件夹上传。排除 .env、runtime、records、密码、原始日志、NVS/整片 Flash、截图和旧构建包。.gitignore 不能删除已经提交的秘密；如已泄露，应先更换凭据并从历史移除。

建议仓库名 esp32c3-incubator-controller，标题 ESP32-C3 Incubator Light Controller & Data Logger。审阅候选版和选择许可证期间先用私有仓库；实机验收后再打 tag/Release。GitHub 会自动生成源码压缩包；二进制须先检查编译默认值和内嵌路径，再作为 Release 附件。

不为过去的实验伪造 Git 提交。版本变化保留在 CHANGELOG.md；有价值的旧源码另行审核后才做历史归档，不批量上传错误试验包和聊天记录。

在 GitHub 新建空仓库后，在这个干净目录按 GitHub 的“已有文件夹”步骤操作；首次提交/推送前检查 git status 和 git diff --cached。不要在含其他项目的父目录执行 git add .。GitHub 登录凭据应通过官方登录流程输入，不贴聊天或写进脚本。

### 发布双语 Wiki

wiki/ 目录可直接作为普通仓库文档阅读。GitHub Wiki 是独立的 仓库名.wiki.git，并不会因为推送 wiki 文件夹就自动建立。先在 GitHub Wiki 页面启用/创建 Home，再克隆 Wiki 仓库；用 tools/export_wiki.py 复制并改写适用于 Wiki 的链接，核对差异后单独提交/推送。工具不会自动推送。

Wiki 是否可用取决于 GitHub 套餐及仓库可见性，参见[Wiki 说明](https://docs.github.com/en/communities/documenting-your-project-with-wikis/about-wikis)、[本地编辑 Wiki](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages)。私有 Wiki 不可用时，主仓库内双语文档仍可阅读。许可证选择参见[GitHub 说明](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository)。

