import json
import os
import sqlite3
import sys
import tempfile
from contextlib import closing
import types
import unittest
from pathlib import Path
from unittest.mock import patch
import recorder
from record_store import Store, compact
from test_record_store import NOW, progress, request

class FakeClient:
    messages=[]
    latest=None
    def __init__(self,*args,**kwargs):
        self.acks=[];self.sent=[];self.closed=False
        assert kwargs["manual_ack"] is True and kwargs["clean_session"] is False
        FakeClient.latest=self
    def username_pw_set(self,*args):self.account=args
    def will_set(self,*args,**kwargs):pass
    def reconnect_delay_set(self,*args,**kwargs):pass
    def max_inflight_messages_set(self,*args,**kwargs):pass
    def max_queued_messages_set(self,*args,**kwargs):pass
    def connect_async(self,*args,**kwargs):pass
    def subscribe(self,*args):pass
    def publish(self,topic,payload,qos=0,retain=False):
        self.sent.append((topic,payload,qos,retain))
        return types.SimpleNamespace(rc=0)
    def disconnect(self):self.closed=True
    def ack(self,mid,qos):
        # A second connection proves the snapshot/request is committed before acknowledging delivery.
        with closing(sqlite3.connect(os.environ["RECORD_DB"])) as db:
            if mid==1:
                assert db.execute("SELECT COUNT(*) FROM snapshots").fetchone()[0]==1
            if mid==2:
                assert db.execute("SELECT COUNT(*) FROM recovery_requests").fetchone()[0]==1
        self.acks.append((mid,qos))
    def loop_forever(self,**kwargs):
        self.on_connect(self,None,None,types.SimpleNamespace(is_failure=False),None)
        for msg in self.messages:
            self.on_message(self,None,msg)
            if self.closed:break

class FailStore(Store):
    def progress(self,*args,**kwargs):raise sqlite3.OperationalError("simulated disk full")

def modules():
    paho=types.ModuleType("paho");mq=types.ModuleType("paho.mqtt");client=types.ModuleType("paho.mqtt.client")
    client.Client=FakeClient;client.CallbackAPIVersion=types.SimpleNamespace(VERSION2=2);client.MQTTv311=4;client.MQTT_ERR_SUCCESS=0
    paho.mqtt=mq;mq.client=client
    return {"paho":paho,"paho.mqtt":mq,"paho.mqtt.client":client}

def message(topic,data,mid):
    return types.SimpleNamespace(topic="incubator/a1b2c3/"+topic,payload=compact(data).encode(),mid=mid,qos=1,retain=False)

class TransportTests(unittest.TestCase):
    def run_service(self,messages,store=Store,secret=None):
        with tempfile.TemporaryDirectory() as tmp:
            env={"MQTT_PASSWORD":"test-only-not-real","MQTT_PASSWORD_FILE":"","RECORD_DB":str(Path(tmp)/"data.sqlite3")}
            if secret is not None:
                key=Path(tmp)/"key.txt";key.write_text(secret+"\n",encoding="utf-8");env["MQTT_PASSWORD_FILE"]=str(key)
            FakeClient.messages=messages
            with patch.dict(sys.modules,modules()),patch.dict(os.environ,env),patch.object(recorder.signal,"signal"),patch.object(recorder,"Store",store),patch.object(recorder.dashboard,"start"),patch.object(recorder.threading,"Thread"):
                recorder.main()
            return FakeClient.latest
    def test_ack_after_durable_commit_and_unretained_reply(self):
        # The server wall-clock is controlled to match the trusted simulated source times.
        with patch.object(recorder.time,"time_ns",return_value=NOW*1000000):
            client=self.run_service([message("progress",progress(),1),message("recovery/request",request(),2)])
        self.assertEqual(client.acks,[(1,1),(2,1)])
        reply=[x for x in client.sent if x[0].endswith("/recovery/reply")][0]
        self.assertTrue(json.loads(reply[1])["found"])
        self.assertEqual(reply[2:],(1,False))
    def test_storage_error_leaves_delivery_unacked_and_exits(self):
        with patch.object(recorder.time,"time_ns",return_value=NOW*1000000),self.assertRaises(SystemExit) as error:
            self.run_service([message("progress",progress(),1)],FailStore)
        self.assertEqual(error.exception.code,1)
        self.assertEqual(FakeClient.latest.acks,[])
        self.assertTrue(FakeClient.latest.closed)
        self.assertTrue(any(x[0].endswith("/logger/availability") and x[1]=="offline" for x in FakeClient.latest.sent))
    def test_local_password_file_preserves_dollar_and_hash(self):
        client=self.run_service([],secret="test-only-$#literal")
        self.assertEqual(client.account,("incubator_logger","test-only-$#literal"))
    def test_bad_protocol_is_acknowledged_without_state_update(self):
        client=self.run_service([message("progress",{},3)])
        self.assertEqual(client.acks,[(3,1)])
        self.assertFalse(any(x[0].endswith("/recovery/reply") for x in client.sent))

if __name__=="__main__":unittest.main(verbosity=2)
