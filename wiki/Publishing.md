# Backups, cloning and version selection

## Select a version

Use the [version catalog](../archive/README.md) to choose a release. Connected-2.2.2 with NAS-Recorder-2.2.2c is the prototype reference pair; 2.2.3 adds the Flash sample journal and remains a candidate pending hardware acceptance. Older versions are for reproducing a specific behavior or studying the implementation. Read the README inside the selected ZIP rather than applying the current wiring or deployment guide to it.

Firmware and recorder versions are separate. A firmware rollback does not require a production recorder rollback. Test old recorders against an independent data directory; never let an old schema writer open a newer production database. Compare the package manifest SHA256 with archive/catalog.json after download.

## Save and restore a board

The PowerShell tools under tools/backup discover the actual partition table, read NVS twice and compare hashes. NVS holds Wi-Fi/MQTT credentials, logical identity, BLE bonds and saved programs. Store dumps securely. The save tool option -FullFlash also preserves the application and filesystem; an NVS-only backup does not contain sensor journal files.

Disconnect the 24 V load and external GPIO, lock the outputs, turn MQTT off and close the serial monitor before entering BOOT download mode. Check the physical board and actual COM port. Save-IncubatorConfig.ps1 accepts -Port and optional -ToolPath; Get-Help lists its parameters. Restore only to a compatible 4 MB ESP32-C3 and partition layout, then verify the read-back. First boot uses USB power alone because saved recovery settings can enable outputs automatically.

For a one-to-one replacement, keep the replaced board offline and preserve the logical ID, ACL and history association. For a separate controller, use firmware that supports device identity changes: disarm, mqtt off, device id new, then restart. Allocate a new NAS device and ACL before enabling MQTT. Earlier versions without this command derive identity from hardware. Restoring NVS does not rewrite the hardware MAC.

## GitHub Wiki maintenance

The bilingual wiki/ directory is directly readable in the main repository. The GitHub Wiki feature uses a separate repository ending in .wiki.git. After initializing Home through the Wiki page, clone that repository and use tools/export_wiki.py to copy the pages and adapt links, then review and push the changes. The exporter never pushes automatically. Wiki availability depends on repository visibility and plan: [GitHub Wiki documentation](https://docs.github.com/en/communities/documenting-your-project-with-wikis/about-wikis).
