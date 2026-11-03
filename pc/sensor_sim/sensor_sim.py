"""Publish simulated sensor readings to an MQTT broker."""

import argparse
import json
import random
import re
import threading
from datetime import datetime, timedelta, timezone

import paho.mqtt.client as mqtt


def timestamp():
    return datetime.now(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M:%S")


def payload(**fields):
    return json.dumps({"timestamp": timestamp(), **fields}, separators=(",", ":"))


def main():
    parser = argparse.ArgumentParser(description="Publish simulated MQTT telemetry")
    parser.add_argument("--host", required=True, help="MQTT broker address")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--interval", type=float, default=5.0, help="seconds")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.device_id):
        parser.error("device ID must contain 1-64 letters, digits, underscores or hyphens")
    if not 1 <= args.port <= 65535 or not 0 < args.interval < float("inf"):
        parser.error("port or interval is invalid")

    prefix = f"edgelink/device/{args.device_id}"
    connected = threading.Event()
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=args.device_id,
                         protocol=mqtt.MQTTv311)
    client.will_set(f"{prefix}/status", payload(state="offline"), qos=1, retain=True)

    def on_connect(client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"Connection rejected: {reason_code}", flush=True)
            return
        client.publish(f"{prefix}/status", payload(state="online"), qos=1, retain=True)
        connected.set()
        print("Connected", flush=True)

    def on_disconnect(client, userdata, disconnect_flags, reason_code, properties):
        connected.clear()
        print(f"Disconnected: {reason_code}", flush=True)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    client.connect_async(args.host, args.port, keepalive=30)
    client.loop_start()
    try:
        while True:
            if connected.wait(timeout=args.interval):
                reading = payload(temperature_c=round(random.uniform(20, 30), 1),
                                  humidity_percent=round(random.uniform(40, 70), 1))
                result = client.publish(f"{prefix}/telemetry", reading, qos=0)
                if result.rc == mqtt.MQTT_ERR_SUCCESS:
                    print(reading, flush=True)
                threading.Event().wait(args.interval)
    except KeyboardInterrupt:
        print("Stopping", flush=True)
    finally:
        if connected.is_set():
            info = client.publish(f"{prefix}/status", payload(state="offline"), qos=1, retain=True)
            info.wait_for_publish(timeout=2)
        client.disconnect()
        client.loop_stop()


if __name__ == "__main__":
    main()
