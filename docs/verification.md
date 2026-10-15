# Verification record / 验证记录

Candidate: Connected-2.2.3 / NAS-Recorder-2.2.3. Verified: 2026-10-16 (Asia/Shanghai).

- NAS Python suite: 141 tests passed locally and in CI on Python 3.12 and 3.13, including commit receipts, duplicate replay, clock repair, NAS/NTP source handling, historical/live isolation, mail retry behavior and the strict 180-second connection-notice boundary. SMTP transport is mocked; these tests send no real mail.
- Public deployment/wiki tooling: 4 tests passed.
- Native C++ page-store tests: passed for capacity, receipts, CRC, torn tails, reboot ordering, adaptive thinning and immutable sample identity. Policy and NAS clock-boundary assertions also passed.
- Official ESP32-C3 target compilation: the five changed firmware modules and the ten existing pure-logic translation units passed. The complete Arduino-ESP32 3.3.12 build and link passed on GitHub Actions after correcting the clock-name constexpr declaration. Program: 1,914,397 / 3,145,728 bytes; static RAM: 67,816 / 327,680 bytes. These static figures do not include allocations made later by Wi-Fi, BLE and tasks.
- All three browser scripts passed JavaScript syntax checks. Both wiring SVGs were rendered and visually reviewed. Public source checks passed for privacy, Markdown links, SVG structure and generated gzip parity.
- Hardware acceptance, actual Docker build on NAS, physical disconnection/restart and complete calibration grid remain pending for this candidate.

The previously selected stable baseline is Connected-2.2.2 with cumulative NAS-Recorder-2.2.2c. Preserve working backups before candidate installation. See the bilingual validation pages.

Evidence for firmware source commit bfc78797e1c99d16ee65c5884c9b5920a86c8fad:

- [Complete firmware build](https://github.com/Hi-Blah/esp32c3-incubator-controller/actions/runs/38764385345)
- [Source, NAS and native logic checks](https://github.com/Hi-Blah/esp32c3-incubator-controller/actions/runs/38764385379)
