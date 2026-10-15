# Private backups and first GitHub publication

### Backup and replacement

The PowerShell tools under tools/backup read the actual partition table, read NVS twice and compare hashes. NVS includes credentials, identities, bonds and saved program state. Keep every dump private. --FullFlash on the save tool also preserves firmware/filesystem; a NVS-only backup does not contain the new sensor journal.

Disconnect 24 V and external GPIO wiring, lock outputs, stop MQTT and close serial monitoring before BOOT download mode. Select the correct physical board and current COM port. Use Save-IncubatorConfig.ps1 with -Port and optional -ToolPath; each script has Get-Help/parameter documentation. Restore requires a compatible 4 MB ESP32-C3 firmware/partition table and verifies bytes read back. First boot after restore remains a USB-only check because saved recovery permissions can restore outputs. Do not restore a whole Flash image indiscriminately onto another board variant.

For an intentional one-for-one replacement, keep the old board offline and preserve the logical ID/ACL/history. For a new independent switch, after restore send disarm, mqtt off, device id new and restart; allocate a new NAS device ID/ACL before enabling MQTT. Hardware MAC is not rewritten by an NVS restore.

### First repository upload

Start with a clean repository containing this reviewed source tree. Do not upload the private development directory wholesale. Keep .env, runtime, records, password files, raw logs, NVS/full-Flash dumps, screenshots and old build archives out of Git. A .gitignore does not remove a secret that was already committed; rotate any exposed credential and remove it from history before publishing.

A suitable repository name is esp32c3-incubator-controller. Keep it private while reviewing the candidate and selecting the project license. The repository title can be ESP32-C3 Incubator Light Controller & Data Logger. After acceptance, create a tag/release for the new version. GitHub automatically creates source archives; attach verified binaries only after checking embedded defaults and build paths.

Do not fabricate commits for old prototypes. Preserve meaningful version information in CHANGELOG.md; audit old source separately before adding a historical tag/archive. Broken private trial packages and chat logs are not useful release assets.

Create the empty repository on GitHub, then follow its existing-folder instructions from this exact directory, inspect git status and git diff --cached before the first commit/push. Never run git add . from a parent directory containing other projects. GitHub credentials must be entered through its own trusted sign-in flow, not pasted into chat or saved in scripts.

### Publishing the bilingual Wiki

The wiki/ directory is readable as ordinary repository documentation. GitHub Wiki uses a separate REPOSITORY.wiki.git repository and is not created by pushing a folder named wiki. First enable/create Home in GitHub's Wiki UI, clone that wiki repository, then use tools/export_wiki.py to copy and rewrite local links for that target. Review the wiki diff and commit/push both language sets. No tool here pushes automatically.

GitHub plan/visibility affects Wiki availability: see [About wikis](https://docs.github.com/en/communities/documenting-your-project-with-wikis/about-wikis) and [editing wiki pages locally](https://docs.github.com/en/communities/documenting-your-project-with-wikis/adding-or-editing-wiki-pages). If private Wiki is unavailable, the bilingual wiki files remain usable in the main repository. License choice guidance: [GitHub licensing](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository).

