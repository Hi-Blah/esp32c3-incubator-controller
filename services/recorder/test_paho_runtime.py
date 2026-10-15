"""Exercise the real vendored paho 2.1 dispatcher without connecting to a broker."""
from contextlib import closing
from importlib.metadata import version
import os
from pathlib import Path
import sqlite3
import struct
import tempfile
import types
import unittest
from unittest.mock import patch
import paho.mqtt.client as mqtt
import recorder
from record_store import Store, compact
from test_record_store import NOW, progress, request

class DiskFailure(Store):
    def progress(self, *args, **kwargs):
        raise sqlite3.OperationalError("simulated disk full")

class PrivateRuntimeTests(unittest.TestCase):
    def test_private_library(self):
        self.assertEqual(version("paho-mqtt"), "2.1.0")
        self.assertTrue(Path(mqtt.__file__).resolve().is_relative_to(
            Path(__file__).resolve().parent / "vendor"))

    def run_service(self, messages, store=Store):
        factory = mqtt.Client
        clients = []
        with tempfile.TemporaryDirectory() as folder:
            database = str(Path(folder) / "records.sqlite3")
            env = {"MQTT_PASSWORD": "test-only", "MQTT_PASSWORD_FILE": "",
                   "RECORD_DB": database}

            def client_factory(*args, **kwargs):
                client = factory(*args, **kwargs)
                client.probe_acks = []
                client.probe_sent = []
                client.probe_closed = False
                clients.append(client)

                def puback(mid):
                    # Read through a second connection, so uncommitted writes cannot pass.
                    with closing(sqlite3.connect(database)) as db:
                        if mid == 1:
                            self.assertEqual(db.execute(
                                "SELECT COUNT(*) FROM snapshots").fetchone()[0], 1)
                        elif mid == 2:
                            self.assertEqual(db.execute(
                                "SELECT COUNT(*) FROM recovery_requests").fetchone()[0], 1)
                    client.probe_acks.append(mid)
                    return mqtt.MQTT_ERR_SUCCESS

                def publish(topic, payload, qos=0, retain=False):
                    client.probe_sent.append((topic, payload, qos, retain))
                    return types.SimpleNamespace(rc=mqtt.MQTT_ERR_SUCCESS)

                def disconnect(*args):
                    client.probe_closed = True
                    return mqtt.MQTT_ERR_SUCCESS

                def loop(**kwargs):
                    for tail, data, mid in messages:
                        topic = ("incubator/a1b2c3/" + tail).encode()
                        packet = struct.pack("!H", len(topic)) + topic
                        packet += struct.pack("!H", mid) + compact(data).encode()
                        client._in_packet = {
                            "command": mqtt.PUBLISH | 2,
                            "packet": packet, "remaining_length": len(packet)}
                        result = client._handle_publish()
                        self.assertEqual(result, mqtt.MQTT_ERR_SUCCESS)
                        if client.probe_closed:
                            break

                client._send_puback = puback
                client.publish = publish
                client.disconnect = disconnect
                client.loop_forever = loop
                return client

            with patch.dict(os.environ, env), patch.object(mqtt, "Client", client_factory), \
                 patch.object(recorder, "Store", store), \
                 patch.object(recorder.signal, "signal"), \
                 patch.object(recorder.time, "time_ns", return_value=NOW * 1000000):
                try:
                    recorder.main()
                finally:
                    self.latest = clients[0]
        return self.latest

    def test_real_dispatcher_ack_only_after_commit(self):
        client = self.run_service([
            ("progress", progress(), 1), ("recovery/request", request(), 2)])
        self.assertEqual(client.probe_acks, [1, 2])
        reply = next(p for p in client.probe_sent if p[0].endswith("/recovery/reply"))
        self.assertEqual(reply[2:], (1, False))

    def test_real_dispatcher_does_not_ack_disk_failure(self):
        with self.assertRaises(SystemExit) as error:
            self.run_service([("progress", progress(), 1)], DiskFailure)
        self.assertEqual(error.exception.code, 1)
        self.assertEqual(self.latest.probe_acks, [])
        self.assertTrue(self.latest.probe_closed)

    def test_real_dispatcher_invalid_record_ack_once(self):
        client = self.run_service([("progress", {}, 3)])
        self.assertEqual(client.probe_acks, [3])
        self.assertFalse(any(p[0].endswith("/recovery/reply") for p in client.probe_sent))

if __name__ == "__main__":
    unittest.main(verbosity=2)
