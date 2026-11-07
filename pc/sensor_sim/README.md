# MQTT 设备模拟器

在 Windows PowerShell 中，进入本目录后安装依赖：

```powershell
python -m pip install -r requirements.txt
```

指定 Broker 地址和设备 ID 启动；间隔单位为秒，默认 5 秒：

```powershell
python sensor_sim.py --host 192.168.7.2 --device-id sensor-01 --interval 5
```

同时启动第二个实例时使用不同的 `--device-id`。使用 Ctrl+C 正常退出。主题及字段以 [协议文档](../../protocol/mqtt-topics.md) 为准。Broker 地址应根据实际网络填写；当前程序不配置用户名、密码或 TLS，适用于受控的局域网测试。

程序使用 MQTT 3.1.1 和 Paho 2.x 回调接口，联网工作由 Paho 线程处理，主线程按单调时钟安排遥测。设备每 10 秒发布在线心跳，因此即使遥测间隔很长，网关也能识别在线状态。异常断线后自动重连，重连成功后重新发布 online。Ctrl+C 或 SIGTERM 退出时发送 offline，最多等待 2 秒确认，再断开连接并停止网络线程。

先阅读 `SensorSimulator.__init__`（配置与回调），再看 `on_connect` / `on_disconnect`，最后看 `run`（上报循环和退出）。

本地无网络检查：

```powershell
python -m unittest test_sensor_sim.py
```

检查使用真实 Paho 客户端对象并替换网络方法，覆盖在线、遥测、离线确认和连接状态清理；它不能代替 Broker 联调。
