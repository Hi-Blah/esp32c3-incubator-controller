# NAS-Recorder-2.2.2c

[简体中文](README.zh-CN.md)

Historical analysis, dashboard and notification runtime. Cumulative a/b/c code, including SMTP envelope/body submission fixes.

This is a complete cumulative source archive, not a patch that requires an undocumented earlier directory. Original templates are retained; compose.archive.yaml is a separate portable example for an isolated test deployment. This historical runtime has not been requalified against every newer firmware or current production database.

## Standalone installation

Prerequisites: Docker Compose v2 and a reachable MQTT broker with authenticated device/logger accounts; Home Assistant is optional. mqtt/config/acl.archive.example provides device-scoped permissions. Replace the example ID and usernames, merge into the broker ACL, then reload it. The archive contains the recorder, not a preconfigured broker.

1. Extract to a new directory, then enter recorder. Read compose.archive.yaml and the environment settings expected at the top of recorder.py. Configure the actual MQTT host/port, device ID and logger username. Use .env.example when supplied. Example IP 192.0.2.10 and example device a1b2c3 must be replaced.
2. Create logger_password.txt locally with the MQTT logger password. If the notifier exists, prepare smtp.json and smtp_password.txt using the fields loaded by notifier.py; smtp.example.json is a template when present. Grant the service read access, with no world-readable secrets. Do not put a password in a command argument.
3. Create records and make it writable by the chosen PUID/PGID (defaults 1000:1000). The included vendor directory contains paho-mqtt 2.1.0 and its license. The portable example uses Python 3.13-slim without pip installs; RECORDER_IMAGE may select a compatible existing local image.
4. Run these as separate commands:

~~~sh
docker compose -f compose.archive.yaml config --quiet
~~~

~~~sh
docker compose -f compose.archive.yaml up -d
~~~

View recorder logs. The dashboard, when supplied, binds localhost:8790 by default; configure a LAN bind or a trusted reverse proxy deliberately.
5. Configure broker ACL for the actual device. Use mqtt/config/acl when supplied and compare the subscriptions/publications in recorder.py. Sensor/state/event/progress/recovery topics vary by version. Browser control, analysis heartbeat and config topics need the corresponding permissions; 2.2.3 additionally uses samples and samples/ack.

## Upgrading an existing installation

Back up the database and existing Compose/environment/secret files. Preserve mounts, image, user, networks, credentials and records. Do not overwrite a working Compose file with an example. Review the version's module set and add required mounts before recreating its services. Do not apply an old recorder to a newer database; use a separate data directory for historical comparisons. Firmware rollback does not require recorder rollback.

## Acceptance and compatibility

Use matching-era firmware first: 2.0.7/2.0.8 packages target those recovery protocols; 2.1.0 adds optical/notification support; 2.2.x supports NAS analysis/control. NAS 2.2.2c is the cumulative reference companion for firmware 2.2.2. Verify fresh state, sample age, CSV, recovery responses and actual SMTP delivery before unattended use. An SMTP login-only check does not test message delivery. Historical source is provided with known limitations; see the repository archive catalog. Project licensing remains unassigned.
