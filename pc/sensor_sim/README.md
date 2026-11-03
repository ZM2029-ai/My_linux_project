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
