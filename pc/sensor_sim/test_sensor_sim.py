"""无网络生命周期检查：使用真实 Paho 对象，替换网络方法。"""
import json
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from sensor_sim import SensorSimulator


class SimulatorLifecycleTest(unittest.TestCase):
    def test_online_telemetry_and_confirmed_offline(self):
        sim = SensorSimulator("localhost", 1883, "sensor-test", 5)
        sent = []
        acknowledged = []

        def publish(topic, value, qos, retain=False):
            sent.append((topic, json.loads(value), qos, retain))
            if topic.endswith("/telemetry"):
                sim.stopping.set()
            return SimpleNamespace(rc=0, wait_for_publish=lambda timeout: acknowledged.append(timeout),
                                   is_published=lambda: True)

        def start_loop():
            sim.on_connect(sim.client, None, None, SimpleNamespace(is_failure=False), None)
            return 0

        with patch.object(sim.client, "connect_async"), patch.object(sim.client, "loop_start", side_effect=start_loop), \
             patch.object(sim.client, "publish", side_effect=publish), patch.object(sim.client, "disconnect") as disconnect, \
             patch.object(sim.client, "loop_stop") as stop_loop:
            sim.run()
        self.assertEqual(sent[0][1]["state"], "online")
        self.assertTrue(any(item[0].endswith("/telemetry") for item in sent))
        self.assertEqual(sent[-1][1]["state"], "offline")
        self.assertEqual(sent[-1][2:], (1, True))
        self.assertEqual(acknowledged, [2])
        disconnect.assert_called_once()
        stop_loop.assert_called_once()

    def test_network_loss_clears_connection(self):
        sim = SensorSimulator("localhost", 1883, "sensor-test", 5)
        sim.connected.set()
        sim.on_disconnect(sim.client, None, None, "connection lost", None)
        self.assertFalse(sim.connected.is_set())


if __name__ == "__main__":
    unittest.main()
