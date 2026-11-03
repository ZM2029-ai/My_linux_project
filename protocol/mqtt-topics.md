# MQTT 主题约定（阶段三）

使用 MQTT 3.1.1，主题中的 `device_id` 由英文字母、数字、下划线或连字符组成，长度为 1～64。每个同时连接的模拟设备必须使用不同的 ID。

| 主题 | 方向 | QoS | 保留消息 | 用途 |
| --- | --- | --- | --- | --- |
| `edgelink/device/{device_id}/telemetry` | 设备 → 网关 | 0 | 否 | 定时遥测 |
| `edgelink/device/{device_id}/status` | 设备 → 网关 | 1 | 是 | 最近一次在线状态 |
| `edgelink/device/{device_id}/command` | 网关 → 设备 | 待阶段六确定 | 待定 | 控制命令，暂未实现 |
| `edgelink/device/{device_id}/response` | 设备 → 网关 | 待阶段六确定 | 待定 | 命令响应，暂未实现 |

载荷使用 UTF-8 JSON，字段见 [device-telemetry.md](device-telemetry.md)。网关可订阅 `edgelink/device/+/telemetry` 和 `edgelink/device/+/status`。

设备连接成功后发布 `online`，正常退出时发布 `offline`。连接前设置保留的 `offline` 遗嘱，以便 Broker 在检测到异常断开时发布。遗嘱的时间是连接前生成的时间，不是断线发生时间；网络分区等情况的离线检测也可能延迟。Broker 无法访问时，保留的状态不能证明设备当前在线。
