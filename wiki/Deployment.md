# NAS Docker installation and upgrade

Use a Linux NAS with Docker Compose v2 and Python 3.12+ for the preparation helper. The runtime image bundles paho-mqtt 2.1.0; it does not need a network pip install. The base Python and Mosquitto images must already exist locally or be downloadable. Image tags can change: record the resolved digests for a reproducible deployment.

### Fresh installation

Copy/clone the entire repository to a directory on the NAS. Run commands from that directory. Read your board's device info; a1b2c3 is a synthetic placeholder, replace it with your six-digit ID.

~~~sh
sudo python3 tools/setup_runtime.py --device a1b2c3 --uid 1000 --gid 1000
~~~

Choose the actual NAS user/group IDs if they differ. The helper asks privately for three separate MQTT passwords, generates ACL/config and private runtime files, and invokes the official mosquitto_passwd converter. It refuses to overwrite .env or runtime. Passwords do not enter command arguments. Do not re-run the password converter on already-hashed files.

~~~sh
sudo docker compose config --quiet
~~~

~~~sh
sudo docker compose up -d --build
~~~

~~~sh
sudo docker compose logs --tail=40 recorder
~~~

Expected: Recorder connected and a database under /records. Configure the ESP32 with the NAS host, port 1883 and incubator_device password. Configure Home Assistant's MQTT integration using incubator_ha. The browser address is http://YOUR-NAS:8790/device/ ; username incubator, password matching the local runtime/secrets/logger_password.txt. Do not send that file to support.

The recorder stores data in runtime/records/incubator.sqlite3. The HTTP UI and unencrypted broker should be accessible only through your trusted network. DNS/VPN/router reachability must work independently of MQTT credentials.

For mail, edit runtime/secrets/smtp.json and populate smtp_password.txt locally. Then start the optional worker:

~~~sh
sudo docker compose --profile mail up -d
~~~

~~~sh
sudo docker compose exec mail python3 -S notifier.py --check
~~~

The second command is read-only and sends nothing. SMTP setup and an explicit one-message delivery test are described in [calibration/notifications](Calibration.md).

### Upgrade an existing NAS without rollback

Keep the existing image, network, user, .env, password files, smtp.json and records. Back up SQLite using its backup API or stop writers before copying; copying only the live main file can omit WAL transactions.

Copy this cumulative services/recorder tree to a new source directory. For a JSON-format existing compose.local.yaml, generate a side-by-side mount update:

~~~sh
python3 tools/upgrade_existing_compose.py --file /YOUR/EXISTING/compose.local.yaml --source /YOUR/NEW/services/recorder
~~~

Inspect and validate the generated compose.local.2.2.3.yaml, then run compose up -d using that file. Traditional YAML is deliberately left unchanged: manually merge new /app/sample_journal.py and the cumulative runtime modules/vendor, preserving all secrets, records, network and image settings. The existing mail worker also needs the cumulative notifier.py.

Add these permissions inside the corresponding existing broker user blocks (replace ID):

~~~text
# user incubator_device
 topic write incubator/a1b2c3/samples
 topic read incubator/a1b2c3/samples/ack
# user incubator_logger
 topic read incubator/a1b2c3/samples
 topic write incubator/a1b2c3/samples/ack
~~~

These are additions, not a replacement for the state/config/analysis/recovery ACL. Reload the broker with HUP only after validating the edited ACL. Upgrade NAS before enabling the new firmware journal; old firmware remains supported. Database changes are additive and do not erase previous records.

