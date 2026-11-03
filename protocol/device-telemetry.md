# 设备遥测与状态

主题中的设备 ID 是消息来源；载荷不重复存放设备 ID。时间采用北京时间（UTC+08:00），格式 `YYYY-MM-DD HH:MM:SS`。

遥测示例（温度单位为摄氏度，湿度单位为相对湿度百分比）：

```json
{"timestamp":"2026-11-02 12:00:00","temperature_c":25.3,"humidity_percent":55.1}
```

状态示例：

```json
{"timestamp":"2026-11-02 12:00:00","state":"online"}
```

`state` 为 `online` 或 `offline`。状态的时间表示该消息生成时间；特别是异常断线遗嘱在连接前生成，不代表实际断线时间。数据均为模拟数据，不持久化。
