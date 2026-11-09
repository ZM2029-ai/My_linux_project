# HTTP API

`edgelink_http DATABASE WEB_DIRECTORY PORT [BIND_ADDRESS]` 默认监听 `127.0.0.1`。启用阶段六控制功能时，构建选项为 `-DEDGELINK_ENABLE_CONTROL=ON`，并通过 `--users FILE --origin ORIGIN [--broker HOST PORT]` 配置账号和浏览器源。此模式强制绑定 `127.0.0.1` 或 `::1`。公网通过 HTTPS 临时隧道访问，局域网开发使用 SSH 本地端口转发。数据为 UTF-8 JSON，查询使用只读 SQLite 连接，控制审计使用读写连接。

账号文件按行写入 `username:role:password`，角色为 `control` 或 `read`。密码至少 12 个字符且不能包含冒号。至少配置一个控制账号。Linux 上文件必须仅允许所有者访问，例如 `chmod 600 users.conf`。查询和控制都要求 HTTP Basic Auth，控制请求还需具有 `control` 角色，且浏览器 `Origin` 必须与启动时配置值完全相同。

示例配置内容为 `operator:control:replace-with-a-long-password`，可另加 `viewer:read:replace-with-a-long-password`。控制模式启动示例为 `edgelink_http data.sqlite3 web 18080 --users /etc/edgelink/users.conf --origin https://your-public-hostname --broker 127.0.0.1 1883`。本地 SSH 转发访问时，把 `--origin` 设为浏览器地址的源，例如 `http://127.0.0.1:18081`。

- `GET /api/devices` 返回 `{"devices": [...]}`。
- `GET /api/devices/{device_id}` 返回单个设备对象。
- `GET /api/devices/{device_id}/status` 返回设备标识和 `presence`。
- `GET /api/devices/{device_id}/telemetry?start=...&end=...&limit=...` 返回 `{"samples": [...], "truncated": false}`。时间采用北京时间 `YYYY-MM-DD HH:MM:SS`，查询范围包含开始、不包含结束。`limit` 默认为 100，范围为 1～1000。`truncated` 为 true 表示还有更多记录。
- `GET /api/session` 返回当前认证用户名和 `can_control` 权限，供网页决定是否显示控制入口。
- `POST /api/devices/{device_id}/commands` 接收 `{"enabled":false}` 或 `{"enabled":true}`，返回 HTTP 202 与 `command_id`、初始 `status`。同一设备已有待执行命令时返回 409，控制 MQTT 未就绪时返回 503。每条命令等待设备回执 5 秒。
- `GET /api/devices/{device_id}/commands/{command_id}` 返回 `command_id`、`device_id`、`enabled`、`status` 和 `message`。`status` 为 `pending`、`succeeded`、`failed` 或 `timed_out`。命令状态保留在 HTTP 进程内最多 10 分钟。

设备对象包含 `device_id`、`display_name`、`registered_at`、`last_received_at`、`presence` 和 `latest`。`latest` 为 null 或遥测对象。遥测对象包含 `id`、`sampled_at`、`received_at`、`temperature_c` 和 `humidity_percent`。最新记录按采样时间、记录 ID 降序选择。

参数无效返回 400，未认证返回 401，权限不足或浏览器 Origin 不匹配返回 403，设备或命令不存在返回 404，数据库异常返回 500。错误响应格式为 `{"error":"..."}`。MQTT 控制主题和载荷见 [控制命令协议](control-command.md)。
