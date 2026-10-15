# Verification record / 验证记录

Candidate: Connected-2.2.3 / NAS-Recorder-2.2.3. Date: 2026-10-15.

- NAS Python suite: 141 tests passed, including commit receipts, duplicate replay, clock repair, NAS/NTP source handling, historical/live isolation, mail retry behavior and the strict 180-second connection-notice boundary. SMTP transport is mocked; these tests send no real mail.
- Public deployment/wiki tooling: 4 tests passed.
- Native C++ page-store tests: passed for capacity, receipts, CRC, torn tails, reboot ordering, adaptive thinning and immutable sample identity. Policy and NAS clock-boundary assertions also passed.
- Official ESP32-C3 target compilation: the five changed firmware modules and the ten existing pure-logic translation units passed. The complete firmware build/link remains pending; these checks do not establish that a flashable image has been produced.
- All three browser scripts passed JavaScript syntax checks. Both wiring SVGs were rendered and visually reviewed. Public source checks passed for privacy, Markdown links, SVG structure and generated gzip parity.
- Hardware acceptance, actual Docker build on NAS, physical disconnection/restart and complete calibration grid remain pending for this candidate.

The previously selected stable baseline is Connected-2.2.2 with cumulative NAS-Recorder-2.2.2c. Preserve working backups before candidate installation. See the bilingual validation pages.
