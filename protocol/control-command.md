# 控制命令与响应协议（阶段六）

命令和响应载荷均为 UTF-8 JSON。主题、方向、QoS 和保留设置见 [MQTT 主题约定](mqtt-topics.md)。时间沿用项目约定，使用开发板本地时间 `Asia/Shanghai`，格式为 `YYYY-MM-DD HH:MM:SS`。

## 命令

网关向 `edgelink/device/{device_id}/command` 发布 QoS 1、非保留消息：

```json
{"command_id":"cmd-001","action":"set_reporting","enabled":false,"timestamp":"2026-11-08 12:00:00"}
```

- `command_id` 是网关生成的 1～64 字符标识，只能包含英文字母、数字、下划线和连字符。它用于关联回执和去重。
- `action` 当前只支持 `set_reporting`。
- `enabled` 必须是 JSON 布尔值。`false` 暂停遥测发布，`true` 恢复遥测发布。
- `timestamp` 必须为有效的本地时间字符串。
- 载荷不能超过 4096 字节，未知或缺少字段的命令无效。

## 响应

设备向 `edgelink/device/{device_id}/response` 发布 QoS 1、非保留消息：

```json
{"command_id":"cmd-001","success":true,"message":"reporting disabled","timestamp":"2026-11-08 12:00:01"}
```

设备拒绝带有有效 `command_id` 但字段或参数不合法的命令时，`success` 为 `false`，`message` 给出简短原因。主题不匹配、保留消息、非 QoS 1 消息、超长载荷和无法解析的 JSON 会被忽略，不产生响应。时间由设备在处理时生成。

## 重复投递

设备保存最多 256 条近期命令的响应。再次收到相同 `command_id` 时，不重复应用命令，而是重发已缓存的原响应。缓存只在设备进程运行期间有效。网关不得把命令或响应设为保留消息，设备以清洁会话连接 Broker。
