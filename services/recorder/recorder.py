"""MQTT transport. Acknowledge recorder deliveries only after durable SQLite commit."""
import logging
import os
import signal
import sqlite3
import sys
import time
import threading
import json,sample_journal
import nas_analysis,analysis_transport,dashboard,device_gateway
from pathlib import Path
from record_store import Store, decode

logging.basicConfig(level=logging.INFO,format="%(asctime)s %(levelname)s %(message)s")

def main():
    import paho.mqtt.client as mqtt
    logging.info("Recorder runtime=NAS-Recorder-2.2.3a clock_repair_batch=%d",sample_journal.REPAIR_BATCH)
    device=os.getenv("DEVICE_ID","a1b2c3")
    secret=os.getenv("MQTT_PASSWORD_FILE","")
    password=Path(secret).read_text(encoding="utf-8").removesuffix("\n").removesuffix("\r") if secret else os.environ.get("MQTT_PASSWORD","")
    if "\n" in password or "\r" in password:
        raise RuntimeError("MQTT password file must contain one line")
    if not password:
        raise RuntimeError("Set logger_password.txt locally; do not send it to chat")
    store=Store(os.getenv("RECORD_DB","/records/incubator.sqlite3"),device)
    base="incubator/"+device
    client=mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,client_id="incubator-recorder-"+device,
                       clean_session=False,protocol=mqtt.MQTTv311,manual_ack=True)
    client.username_pw_set(os.getenv("MQTT_USERNAME","incubator_logger"),password)
    client.will_set(base+"/logger/availability","offline",qos=1,retain=True)
    client.reconnect_delay_set(min_delay=2,max_delay=60)
    client.max_inflight_messages_set(4)
    client.max_queued_messages_set(32)
    failed=[False]
    connected=threading.Event();stopping=threading.Event()
    gateway=device_gateway.Gateway(client,device)
    dashboard_server=dashboard.start(os.getenv("RECORD_DB","/records/incubator.sqlite3"),device,password,gateway)
    def monitor():
        db=sqlite3.connect(os.getenv("RECORD_DB","/records/incubator.sqlite3"),timeout=3)
        while not stopping.wait(5):
            try:
                with db:analysis_transport.tick(client,db,device,time.time_ns()//1000000,connected.is_set())
                sample_journal.repair_pending(db,device)
            except sqlite3.Error as exc:
                logging.warning("NAS monitor database retry: %s",getattr(exc,"sqlite_errorname",type(exc).__name__))
        db.close()
    threading.Thread(target=monitor,daemon=True).start()
    def on_connect(c,userdata,flags,reason_code,properties):
        if reason_code.is_failure:
            logging.error("MQTT connection refused: %s",reason_code)
            return
        c.subscribe([(base+"/"+tail,1) for tail in ("state","progress","telemetry","events","availability","recovery/request","analysis/command","analysis/device_reply","config","samples")])
        connected.set()
        c.publish(base+"/logger/availability","online",qos=1,retain=True)
        analysis_transport.discovery(c,device)
        logging.info("Recorder connected; device=%s database=%s",device,os.getenv("RECORD_DB","/records/incubator.sqlite3"))
    def on_message(c,userdata,msg):
        import json
        now=time.time_ns()//1000000
        started=time.monotonic()
        try:
            if msg.topic==base+"/availability":
                store.availability(msg.topic,msg.payload.decode("ascii"),now,msg.retain)
            else:
                data=decode(msg.payload)
                if msg.topic==base+"/samples":
                    receipt=sample_journal.ingest(store.db,device,data,now,msg.retain)
                    c.publish(base+"/samples/ack",json.dumps(receipt,separators=(",",":")),qos=1,retain=False)
                elif msg.topic==base+"/analysis/device_reply":
                    gateway.reply(data,msg.retain)
                elif msg.topic==base+"/progress":
                    store.progress(data,now,msg.retain)
                elif msg.topic==base+"/state":
                    store.state(data,now,msg.retain)
                    if not msg.retain:analysis_transport.results(c,store.db,device,now)
                elif msg.topic==base+"/config":
                    cfg=nas_analysis.integer(data.get("cfg"),1,2**32-1)
                    with store.db:store.db.execute("INSERT OR REPLACE INTO device_configs VALUES(?,?,?,?)",(device,cfg,msg.payload.decode("utf8"),now))
                elif msg.topic==base+"/analysis/command":
                    if msg.retain:raise ValueError("retained analysis command rejected")
                    with store.db:ack=nas_analysis.command(store.db,device,data,now)
                    import json
                    c.publish(base+"/analysis/ack",json.dumps(ack,separators=(",",":")),qos=0,retain=False)
                elif msg.topic==base+"/recovery/request":
                    reply=store.response(data,now,msg.retain)
                    info=c.publish(base+"/recovery/reply",reply,qos=1,retain=False)
                    logging.info("Recovery request boot=%s found=%s queued=%s",data["boot"],decode(reply.encode())["found"],info.rc==mqtt.MQTT_ERR_SUCCESS)
                elif msg.topic in (base+"/events",base+"/telemetry"):
                    store.event(msg.topic,data,now,msg.retain)
            if msg.qos:
                c.ack(msg.mid,msg.qos)
        except (ValueError,UnicodeError,KeyError) as exc:
            logging.warning("Rejected %s: %s",msg.topic,exc)
            if msg.topic==base+"/analysis/command":
                import json
                c.publish(base+"/analysis/ack",json.dumps({"ok":False,"message":str(exc)},separators=(",",":")),qos=0,retain=False)
            if msg.qos:
                c.ack(msg.mid,msg.qos)
        except (sqlite3.Error,OSError):
            # Do not ACK a record which has not been saved. Exit so restart/reconnect redelivers QoS1.
            failed[0]=True
            logging.exception("Storage failure; leaving MQTT delivery unacknowledged")
            c.publish(base+"/logger/availability","offline",qos=0,retain=True)
            c.disconnect()
        finally:
            elapsed=time.monotonic()-started
            if elapsed>=1:logging.warning("Slow recorder delivery: topic=%s elapsed_ms=%d",msg.topic,int(elapsed*1000))
    def on_disconnect(c,userdata,flags,reason_code,properties):
        connected.clear()
        if not stopping.is_set():logging.warning("Recorder MQTT disconnected: reason=%s",reason_code)
    client.on_disconnect=on_disconnect
    client.on_connect=on_connect
    client.on_message=on_message
    def stop(*_):
        stopping.set()
        client.publish(base+"/logger/availability","offline",qos=1,retain=True)
        client.disconnect()
    signal.signal(signal.SIGTERM,stop)
    signal.signal(signal.SIGINT,stop)
    client.connect_async(os.getenv("MQTT_HOST","incubator-mqtt"),int(os.getenv("MQTT_PORT","1883")),keepalive=60)
    try:
        client.loop_forever(retry_first_connection=True)
    finally:
        stopping.set();dashboard_server.shutdown();dashboard_server.server_close()
        store.close()
    if failed[0]:
        sys.exit(1)

if __name__=="__main__":
    main()
