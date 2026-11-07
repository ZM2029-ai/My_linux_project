"""阶段三模拟设备：连接 -> 发布在线/心跳 -> 定时遥测 -> 发布离线。"""

import argparse
import json
import math
import random
import re
import signal
import threading
import time
from datetime import datetime, timedelta, timezone

import paho.mqtt.client as mqtt


def payload(**fields):
    timestamp = datetime.now(timezone(timedelta(hours=8))).strftime("%Y-%m-%d %H:%M:%S")
    return json.dumps({"timestamp": timestamp, **fields}, separators=(",", ":"))


class SensorSimulator:
    STATUS_INTERVAL = 10.0

    def __init__(self, host, port, device_id, interval):
        self.host, self.port = host, port
        self.prefix = f"edgelink/device/{device_id}"
        self.interval = interval
        self.connected = threading.Event()
        self.stopping = threading.Event()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id=device_id, clean_session=True, protocol=mqtt.MQTTv311)
        # 遗嘱的 timestamp 是生成时间，不是实际断线时间。
        self.client.will_set(f"{self.prefix}/status", payload(state="offline"), qos=1, retain=True)
        self.client.max_queued_messages_set(20)
        self.client.reconnect_delay_set(min_delay=1, max_delay=30)
        self.client.on_connect = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_connect_fail = self.on_connect_fail

    def publish_status(self, state):
        return self.client.publish(f"{self.prefix}/status", payload(state=state), qos=1, retain=True)

    def on_connect(self, client, userdata, flags, reason_code, properties):
        if reason_code.is_failure:
            print(f"[mqtt] Connection rejected: {reason_code}", flush=True)
            return
        if not self.stopping.is_set():
            self.publish_status("online")
            self.connected.set()
            print("[mqtt] Connected; online status queued", flush=True)

    def on_connect_fail(self, client, userdata):
        print("[mqtt] Cannot reach broker; retrying automatically", flush=True)

    def on_disconnect(self, client, userdata, flags, reason_code, properties):
        self.connected.clear()
        print(f"[mqtt] Disconnected: {reason_code}", flush=True)

    def run(self):
        self.client.connect_async(self.host, self.port, keepalive=30)
        if self.client.loop_start() != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError("Cannot start MQTT network thread")
        next_telemetry = next_status = 0.0
        try:
            while not self.stopping.is_set():
                if not self.connected.wait(timeout=0.2):
                    next_telemetry = next_status = 0.0
                    continue
                now = time.monotonic()
                if now >= next_status:
                    self.publish_status("online")
                    next_status = now + self.STATUS_INTERVAL
                if now >= next_telemetry:
                    reading = payload(temperature_c=round(random.uniform(20, 30), 1),
                                      humidity_percent=round(random.uniform(40, 70), 1))
                    info = self.client.publish(f"{self.prefix}/telemetry", reading, qos=0)
                    if info.rc == mqtt.MQTT_ERR_SUCCESS:
                        print(f"[telemetry] {reading}", flush=True)
                    else:
                        print(f"[mqtt] Telemetry not queued: {info.rc}", flush=True)
                    next_telemetry = now + self.interval
                self.stopping.wait(0.1)
        except KeyboardInterrupt:
            print("[mqtt] Stopping", flush=True)
        finally:
            self.stopping.set()
            try:
                if self.connected.is_set():
                    info = self.publish_status("offline")
                    if info.rc == mqtt.MQTT_ERR_SUCCESS:
                        info.wait_for_publish(timeout=2)
                        if not info.is_published():
                            print("[mqtt] Offline status acknowledgement timed out", flush=True)
            except (RuntimeError, ValueError) as error:
                print(f"[mqtt] Offline status could not be confirmed: {error}", flush=True)
            finally:
                self.client.disconnect()
                self.client.loop_stop()


def main():
    parser = argparse.ArgumentParser(description="Publish simulated MQTT telemetry")
    parser.add_argument("--host", required=True, help="MQTT broker address")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--interval", type=float, default=5.0, help="telemetry interval in seconds")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", args.device_id):
        parser.error("device ID must contain 1-64 letters, digits, underscores or hyphens")
    if not args.host.strip() or not 1 <= args.port <= 65535:
        parser.error("invalid host or port")
    if not math.isfinite(args.interval) or args.interval <= 0:
        parser.error("interval must be a finite positive number")

    def stop_signal(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop_signal)
    SensorSimulator(args.host, args.port, args.device_id, args.interval).run()


if __name__ == "__main__":
    main()
