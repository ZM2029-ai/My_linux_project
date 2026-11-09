# web —— 网页管理端

首页显示设备、在线状态和最新遥测，详情页提供按北京时间查询历史温湿度曲线。页面通过网关的 HTTP API 获取数据，接口见 [HTTP 查询接口](../protocol/http-api.md)。

阶段五构建运行 `edgelink_http DATABASE WEB_DIRECTORY PORT` 后，从 Windows 建立 SSH 本地端口转发，将本机端口映射到板端 `127.0.0.1:PORT`。阶段六构建还需用 `-DEDGELINK_ENABLE_CONTROL=ON` 启用受保护控制，配置账号文件、允许的浏览器 Origin 和 Broker。启动参数与认证配置见 [HTTP API 约定](../protocol/http-api.md)。

页面使用原生 HTML、CSS、JavaScript，每 10 秒刷新设备状态。控制账号可在设备详情页暂停或恢复模拟设备遥测，并查看设备回执。历史查询最多显示 1000 条，超出时提示缩小时间范围。
