# Resource budget / 资源预算

Measured from ESP32-C3 target objects using Arduino-ESP32 3.3.12 / GCC 14.2.0, optimization Os. These are compile-time figures; runtime watermarks still require hardware acceptance.

| New journal allocation | Bytes / 字节 |
|---|---:|
| Fixed Store including 64-page index | 1744 |
| Reused ControlSnapshot | 976 |
| Last sample record | 124 |
| Receipt, status, counters and other module state | 174 |
| Module static RAM total before linker alignment | 3018 |

No task or task stack is added. MQTT serialization reuses the existing 2048-byte publish buffer. File streams use unbuffered I/O; LittleFS/libc and network allocations still have their own dynamic overhead. The 2048-record capacity is Flash capacity, not a 2048-record RAM allocation.

| Known project stack frames | Bytes |
|---|---:|
| Logger worker | 656 |
| sampleJournalTick, including inlined compaction code | 720 |
| Page filter | 560 |
| Page append | 464 |
| Page erase | 224 |
| Sample serialization | 368 |

A known logger append/erase path sums to 2064 bytes of project frames within the existing 4096-byte logger stack. SDK/libc calls and interrupts are not included, so this is not a proof of the complete worst case. Check log_stack and the other task watermarks on hardware during dense capture, compaction, reconnect and calibration.

Huge APP reserves 3MiB for the app and 896KiB for LittleFS. Raw sample payload capacity is 64×32×124=253952 bytes, plus page headers, filesystem metadata and copy-on-write overhead. Existing event files share the partition; 64KiB is kept free and oldest sample pages may be overwritten under pressure. The complete ESP32-C3 build passed: program storage 1,914,397 / 3,145,728 bytes (60%); static RAM 67,816 / 327,680 bytes (20%), leaving a link-time balance of 259,864 bytes. That balance is not runtime free heap: Wi-Fi, BLE, task stacks and other dynamic allocations are made later. Runtime heap minimum and largest free block still require hardware diagnostics. The linker data-RAM budget also excludes other chip memory uses; it is not a statement that all chip SRAM is available as heap.
