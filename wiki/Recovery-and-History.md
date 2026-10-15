# Restart recovery and offline history

## Independent channel policies

| Mode | Recovery |
|---|---|
| Manual | manualEnabled restores saved state after permitted normal boots; a saved lock/off state does not authorize turning on |
| Daily | dailyEnabled evaluates the current local time after synchronization; missed windows are not replayed |
| Cycle | enabled resumes; countOutage advances or pauses elapsed progress; restartAfterSeconds restarts after long gaps |
| NAS assistance | nasEnabled requests the previous boot's matching configuration; identity, request and revision must match |

Panic, watchdog and brownout resets do not authorize ordinary restoration. New configurations enable manual/daily recovery; upgrades retain existing selections. Check recovery info. disarm cancels execution authorization.

## Adaptive Flash journal

This journal is separate from the 64-event queue. It persists across loss of power in the same 4MB Flash that holds firmware. RAM holds a working snapshot and page index, not the history.

| Powered elapsed time since detecting NAS loss | Capture period | Unuploaded cache |
|---|---|---|
| Under 3 minutes | 1 second | Keep dense samples |
| 3 to under 15 minutes | 3 seconds | Gradually thin earlier samples in this outage to about 3 seconds |
| 15 minutes onward | 10 seconds | Gradually thin this outage to about 10 seconds |
| Online | 10 seconds by default | Never delete history already stored on NAS |

Boot boundaries, clock anchors and output/mode/PWM/config changes are retained in addition, so the average can be denser. Silent heartbeat expiry can take about 30 seconds. After reboot, the latest durable outage identifier and powered elapsed time carry forward. Unknown unpowered time cannot count as measured uptime; a not-yet-durable tail can be lost. Repeated offline boots do not indefinitely restart the dense phase.

Records hold temperature/humidity with validity and sample age; both raw ADCs with validity; outputs, modes, PWM frequency/duty, configuration revision, boot identity/order, monotonic uptime and outage stage. This is not a PWM waveform recorder or a capture of every electrical edge.

The ring has 64 pages of 32 records, 124 bytes each, CRC checked and flushed: at most 2048 records. At a uniform 10-second period this is roughly 5h41m, before boundary records, partial pages, reboots, filesystem pressure and damaged tails. Huge APP provides an 896KiB LittleFS partition, not half of 4MB. Keep 64KiB free; capacity pressure overwrites the oldest sample pages rather than formatting storage.

Streaming compaction handles at most two pages per step: write a temporary page, flush, atomically replace, then remove sources. Original sample identities and contents remain unchanged. thinned counts intentional downsampling; covered counts capacity overwrites; failures/damaged count I/O or CRC faults. Counters save once per minute and can lose their latest increment on power failure. A damaged tail is truncated at its first invalid record.

## Commit receipts and time

A matching NAS receipt after the SQLite transaction commits releases a sample; broker PUBACK alone does not. Lost receipts or reboots may replay samples. NAS deduplicates by device/boot/sequence. The original event queue retains its broker-acknowledgement semantics.

- A NAS heartbeat matching this boot, echoed uptime, epoch bounds and a maximum 15-second round trip directly synchronizes the device clock: source NAS. A midpoint approximation still has network uncertainty. NTP provides another source when NAS is unavailable.
- Earlier samples in the same boot can be placed using monotonic uptime. CSV distinguishes nas, ntp, backfilled-nas, backfilled-ntp and unknown. Existing NTP history anchors are not downgraded by a later NAS estimate.
- A wholly offline earlier boot with no anchor cannot acquire an exact outage duration from a later reboot. Its absolute timestamps stay blank; boot_order, boot_id, uptime_ms and sequence retain order and within-boot spacing.

Backfill never updates nas_live, restarts a light program or triggers stale alarms. NAS charts use capture time. Raw ADC remains raw instead of applying today's calibration to old readings. Unknown-time segments remain exportable in order but are omitted from absolute-time charts. HA native history is not backfilled automatically.

## Connection notices

NAS creates one informational outage notification only after more than 180 seconds without a valid live state. Fresh live data then creates the recovery notice. Outages ending within three minutes create neither email. UI freshness and command handshake limits remain unchanged. An independent mail container and configured SMTP are required.

Use USB samples info and the NAS journal/CSV panel. An empty 0/64 event queue does not imply an empty sample journal. Do not format or change partitions while history is pending.

Restoring an older NVS backup or erasing NVS can reset boot order. The counter is monotonic only within an unchanged configuration identity. Use boot_id, known timestamps and replacement records to reconcile device replacements; never run two boards with the same logical ID concurrently.
