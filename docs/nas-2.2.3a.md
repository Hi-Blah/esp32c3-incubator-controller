# NAS recorder 2.2.3a: bounded history clock repair

[简体中文](nas-2.2.3a-zh-CN.md)

This is a NAS-only fix for an existing NAS-Recorder-2.2.3 installation. Keep Connected-2.2.3 firmware, broker permissions, Compose configuration, credentials and the production database.

## Problem and resulting behavior

The previous journal importer ran correlated updates over the current boot's complete temperature/humidity and optical history for every anchored sample. The lookup lacked an appropriate index. As history grew, a single delivery could occupy the writer and the MQTT callback for longer than the recorder's 60-second keepalive. A broker timeout after approximately 90 seconds, repeated reconnects and database-busy monitor messages can result. The recorder client and the physical device client are different connections; identify the client in the broker log before blaming the wireless link.

The importer now selects only unresolved timestamps or NAS timestamps eligible for NTP refinement through partial indexes. It repairs at most 128 journal entries per transaction and updates corresponding historical rows by primary key. The monitor continues pending repairs in bounded batches. It re-reads the anchor under a short write transaction so an earlier NAS estimate cannot overwrite a newer NTP anchor. Existing trusted NTP history is not repeatedly rewritten. Receipt-after-commit, same-boot timestamp boundaries and historical/live alert isolation are preserved.

The recorder also reports slow deliveries, actual database error codes and MQTT disconnect reasons. `/assets/ready` reports `NAS-Recorder-2.2.3a`.

## Upgrade an existing 2.2.3 installation

1. Keep a consistent SQLite backup and backup copies of the current `sample_journal.py`, `recorder.py` and `dashboard.py`.
2. Replace those three files in the source directory mounted by the recorder. With the preceding side-by-side upgrade, this is `upgrade-2.2.3/recorder/` inside the existing recorder directory. Do not put the patch only in the obsolete source directory.
3. In the existing Compose directory, recreate only the recorder so file bind mounts pick up the replacements:

~~~sh
sudo docker compose -f compose.local.2.2.3.yaml up -d --no-build --pull never --force-recreate recorder
~~~

4. Check the startup log and `/assets/ready`, then observe broker logs and live state age for at least 10 minutes. Ordinary reconnection can occur during the restart; repeated 90-second recorder timeouts and database-busy warnings should stop. Continue investigating the device/network independently if it still disconnects.

Do not delete historical rows, WAL/SHM files, configuration or secrets to resolve this issue. The two small indexes are created automatically; no manual database rebuild or `VACUUM` is required. Large unresolved sets are repaired progressively rather than in one long transaction.

## Verification and limits

On 2026-10-18, 148 NAS tests passed locally with the bundled paho-mqtt 2.1.0. New regressions cover bounded query work, trusted-history preservation, incremental repair, rollback without a receipt and concurrent NTP anchor refinement. Two of the regressions reproduce failures against the original 2.2.3 importer.

A synthetic same-boot benchmark with 2,133 journal rows, 48,649 environmental rows and 205,602 optical rows interrupted the original importer after 100 seconds without completion. The fixed importer completed the same new trusted sample in approximately 16 ms on the development workstation. These are synthetic workstation measurements, not measurements of the production database or NAS hardware. Actual recovery and long-duration stability still require deployment observation.
