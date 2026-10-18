# Connected-2.2.3b: local web resource handling

[中文](firmware-2.2.3b-zh-CN.md)

This firmware hotfix addresses two local HTTP defects in 2.2.3. It is a candidate pending hardware acceptance. NAS-Recorder-2.2.3a remains compatible; this firmware change does not require another recorder, Compose, ACL or database update.

The landing page previously polled the full `/api/state` document, including both programs and diagnostics. It now uses an authenticated `/api/entry` response of less than 512 bytes for routing, clock, Wi-Fi and provisioning. Both handlers reuse the existing single HTTP snapshot; no second program snapshot is allocated.

The landing page's Content Security Policy previously allowed connections only to its own origin, blocking its readiness probe to the configured NAS. Its policy now permits that NAS origin as well. The full local UI and APIs retain their same-origin policy and existing authentication/CSRF checks. Configure `INC_NAS_WEB_ORIGIN` in `web_entry.h` before compiling.

Pages and state responses check available heap before sending successful headers. A busy board returns HTTP 503 with `Retry-After: 10`, a short explanation and, for pages, the NAS link. If heap falls during a body transfer, continuous memory waiting is limited to about 100 ms instead of holding the connection for the full 15-second page deadline. The existing 512-byte write limit and memory reserves are unchanged. Socket transfer deadlines still apply independently.

This does not add RAM or guarantee that the full fallback UI can run alongside all network activity. Use the NAS `/device/` page for normal operation; `/local` remains the fallback. A 503 is a resource refusal, not a failed lamp command.

The investigation found that closing board HTTP tabs restored fresh NAS updates (0.7-second sample age) with about 25 KB free heap; the historic minimum was 648 bytes. This supports HTTP resource pressure as a contributor, but does not establish the cause of every MQTT disconnect. Recorder database timeout repair is documented separately in [NAS 2.2.3a](nas-2.2.3a.md).

Acceptance: first observe NAS/HA without board tabs, then open only the board landing page and confirm it redirects to the configured NAS. Test `/local` separately: a complete page or explicit busy response must replace prolonged incomplete loads. Compare board boot ID, cached heap, data age and broker disconnect logs before and after. No firmware upload or physical validation has been performed by the development tools.
