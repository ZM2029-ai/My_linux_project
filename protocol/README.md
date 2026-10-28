# protocol —— 三端共用的协议契约

这里是 **gateway（C++）/ pc（Python）/ web（JS）三方共同遵守的接口定义**。

规则：**任何跨端通信的东西，先在这里定义，再在两端各自实现。** 不允许某端私自定义字段名后让另一端去猜。

## 计划包含的内容

| 文件/目录 | 对应阶段 | 内容 |
| --- | --- | --- |
| `mqtt-topics.md` | 阶段三 | MQTT 主题命名、QoS、载荷 JSON 字段定义 |
| `device-telemetry.md` | 阶段三/四 | 遥测数据字段、类型、单位、时间格式 |
| `control-command.md` | 阶段六 | 控制命令参数、命令 ID、响应与超时约定 |
| `music-stream.md` | 阶段二 | TCP 音乐流的分帧格式（长度/序号/采样参数） |
| `http-api.md` | 阶段五 | REST 接口路径、请求参数、响应结构、错误码 |

## 已有的消息主题（阶段三初步设计，待细化）

```text
edgelink/device/{device_id}/telemetry
edgelink/device/{device_id}/status
edgelink/device/{device_id}/command
edgelink/device/{device_id}/response
```

## 通用约定

1. **时间**：统一使用开发板本地时间 `Asia/Shanghai (CST +0800)`，文本格式 `YYYY-MM-DD HH:MM:SS`。
2. **数值**：遥测值使用浮点并显式标注单位（如 `"temperature": 26.5, "unit": "celsius"`）。
3. **编码**：文本一律 UTF-8。
4. **兼容性**：接口变更时同步修改本目录文档，并在两端同一次提交中更新。

## 当前状态

本目录只有本说明，具体定义在各阶段实现前补写。
