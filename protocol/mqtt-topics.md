# MQTT 主题约定（阶段三）

使用 MQTT 3.1.1，主题中的 `device_id` 由英文字母、数字、下划线或连字符组成，长度为 1～64。每个同时连接的模拟设备必须使用不同的 ID。

| 主题 | 方向 | QoS | 保留消息 | 用途 |
| --- | --- | --- | --- | --- |
| `edgelink/device/{device_id}/telemetry` | 设备 → 网关 | 0 | 否 | 定时遥测 |
| `edgelink/device/{device_id}/status` | 设备 → 网关 | 1 | 是 | 最近一次在线状态 |
| `edgelink/device/{device_id}/command` | 网关 → 设备 | 1 | 否 | 阶段六控制命令 |
| `edgelink/device/{device_id}/response` | 设备 → 网关 | 1 | 否 | 阶段六命令响应 |

载荷使用 UTF-8 JSON。遥测字段见 [device-telemetry.md](device-telemetry.md)，控制命令与响应字段见 [control-command.md](control-command.md)。网关采集进程订阅 `edgelink/device/+/telemetry` 和 `edgelink/device/+/status`。阶段六 HTTP 控制服务另行订阅响应主题。

设备连接成功后发布 `online`，正常退出时发布 `offline`。连接前设置保留的 `offline` 遗嘱，以便 Broker 在检测到异常断开时发布。遗嘱的时间是连接前生成的时间，不是断线发生时间；网络分区等情况的离线检测也可能延迟。Broker 无法访问时，保留的状态不能证明设备当前在线。

模拟器每 10 秒再发布一次在线状态作为心跳。网关收到实时状态或遥测后确认在线，60 秒没有实时消息则标为 `unknown`。网关与 Broker 断开时，全部缓存设备状态标为 `unknown`；重连后等待实时消息重新确认。保留的 `online` 不直接确认在线；保留的 `offline` 可作为最近离线报告，但不覆盖同一连接期间已收到的实时消息。缓存最多容纳 256 个设备，新设备超过上限时拒绝并记录日志。

控制与响应消息均不保留，避免新连接的设备执行历史控制命令。QoS 1 可能重复交付，设备按 `command_id` 对近期处理结果去重。具体行为和格式见 [control-command.md](control-command.md)。
