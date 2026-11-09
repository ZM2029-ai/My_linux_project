# 阶段五：HTTP API 与网页管理端复现

2026-11-08 在 Ubuntu 虚拟机完成 ARM 交叉编译，并在 Debian 10 i.MX6ULL 开发板上完成 MQTT、SQLite、HTTP 和浏览器联调。Windows 模拟设备通过 Tailscale 上的 SSH 端口转发连接测试 Broker，浏览器通过同一 SSH 连接访问板端 HTTP 服务。

## 1. 数据如何流动

```text
Windows 模拟设备 ── SSH 转发 ──► 板端 Mosquitto Broker
                                     │ MQTT
                                     ▼
                               edgelink_mqtt
                                     │ 写入
                                     ▼
                                SQLite 数据库
                                     │ 只读查询
                                     ▼
Windows 浏览器 ──── SSH 转发 ──► edgelink_http
                                     │
                                网页静态文件
```

HTTP 是浏览器和服务端交换请求、响应的协议。浏览器首次访问时，板端 HTTP 服务返回 HTML、CSS 和 JavaScript。网页执行 JavaScript，通过 HTTP API 请求设备数据。服务从 SQLite 读取数据并以 JSON 返回，浏览器再渲染列表和曲线。

例如，`GET /api/devices` 请求设备列表。`GET` 表示读取资源，`/api/devices` 是资源路径。JSON 是一种文本数据格式，可用对象、数组、字符串和数字表示数据。设备详情、状态与历史查询的完整字段、时间边界和错误响应见 [HTTP 查询接口](../../protocol/http-api.md)。

SQLite 的数据表、存储和历史查询见 [阶段四复现文档](../阶段四/阶段四-设备管理与SQLite存储复现.md)。MQTT 的发布、订阅和状态机制见 [阶段三复现文档](../阶段三/阶段三-MQTT消息通信复现.md)。

## 2. 环境与依赖

| 位置         | 本次使用的环境                                                                                                               |
| ---------- | --------------------------------------------------------------------------------------------------------------------- |
| Windows    | PowerShell、OpenSSH、Python 及 `pc/sensor_sim/requirements.txt` 中的依赖，浏览器访问管理页面。                                          |
| Ubuntu 虚拟机 | Debian 10 交叉工具链 GCC 8.3、CMake 和阶段四准备的 ARM SQLite、Mosquitto 与 JSON 开发文件。                                               |
| 开发板        | Debian 10 armhf，运行 SQLite、Mosquitto 和对应运行库。                                                                           |
| 源码         | 网关为 C++14。HTTP 使用仓库中 `gateway/vendor/cpp-httplib` 的 v0.18.7，JSON 使用已有的 nlohmann/json 头文件。前端为原生 HTML、CSS 和 JavaScript。 |

Windows 到两台设备使用已经配置好的 Tailscale SSH 别名 `zm-tailscale` 和 `debian-board-tailscale`。别名、主机密钥与当前地址应以设备 SSH 配置为准。本次实际连接的目标分别为 `100.107.203.116` 和 `100.95.101.37`，地址可能改变。

本次普通 Ubuntu SSH 账号使用阶段四准备的 `/home/zm/edgelink-stage4.PRyv72` 下的工具链文件、`host-compat` 和 SQLite armhf 文件。若这些文件已不存在，请按 [阶段四复现文档的依赖准备步骤](../阶段四/阶段四-设备管理与SQLite存储复现.md#32-重新准备独立构建依赖)重新准备，并将以下 `BASE` 改为新的路径。工具链的首次搭建见 [阶段一复现文档](../阶段一/阶段一-交叉编译环境搭建与验证.md)。

## 3. 传输源码并交叉编译

下面的目录 `~/edgelink-stage5-repro` 是独立复现目录。若该目录已存在，请先检查其中内容，并将本节及后续命令中的目录名一并改成新的名字。

在 Windows 项目根目录 PowerShell 执行：

```powershell
cd E:\Linux_project\demo1
$cfg = Join-Path $env:USERPROFILE '.ssh\ssh-devices\config'
$archive = Join-Path $env:TEMP 'edgelink-stage5-repro-source.tar'
tar -cf $archive gateway web
if ($LASTEXITCODE -ne 0) { throw '源码打包失败' }

ssh -F $cfg -T zm-tailscale 'test ! -e ~/edgelink-stage5-repro && mkdir ~/edgelink-stage5-repro'
if ($LASTEXITCODE -ne 0) { throw 'Ubuntu 目录已存在或创建失败，请选用新目录' }
scp -F $cfg $archive 'zm-tailscale:edgelink-stage5-repro/source.tar'
if ($LASTEXITCODE -ne 0) { throw '源码上传失败' }
ssh -F $cfg zm-tailscale
```

进入 Ubuntu 后执行。此处把 HTTP 和 MQTT 构建到两个独立目录，使用相同的源码快照：

```bash
cd ~/edgelink-stage5-repro
tar -xf source.tar
BASE=/home/zm/edgelink-stage4.PRyv72
export LD_LIBRARY_PATH="$BASE/host-compat"

test -x /opt/buster/usr/bin/arm-linux-gnueabihf-g++-8
test -f "$BASE/relocated-arm.cmake"
test -f "$BASE/sqlite-armhf/usr/include/sqlite3.h"
test -e "$BASE/sqlite-armhf/usr/lib/arm-linux-gnueabihf/libsqlite3.so"

cmake -S gateway -B arm-http \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/relocated-arm.cmake" \
  -DEDGELINK_HTTP_ONLY=ON -DEDGELINK_BUILD_STORAGE_CHECKS=ON \
  -DSQLite3_INCLUDE_DIR="$BASE/sqlite-armhf/usr/include" \
  -DSQLite3_LIBRARY="$BASE/sqlite-armhf/usr/lib/arm-linux-gnueabihf/libsqlite3.so" \
  -DCMAKE_SKIP_RPATH=ON
cmake --build arm-http -- -j2

cmake -S gateway -B arm-mqtt \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/relocated-arm.cmake" \
  -DEDGELINK_ENABLE_MQTT=ON -DEDGELINK_MQTT_ONLY=ON \
  -DEDGELINK_ENABLE_SQLITE=ON -DEDGELINK_BUILD_MQTT_CHECKS=ON \
  -DSQLite3_INCLUDE_DIR="$BASE/sqlite-armhf/usr/include" \
  -DSQLite3_LIBRARY="$BASE/sqlite-armhf/usr/lib/arm-linux-gnueabihf/libsqlite3.so" \
  -DCMAKE_SKIP_RPATH=ON
cmake --build arm-mqtt -- -j2

file arm-http/src/http/edgelink_http
readelf -d arm-http/src/http/edgelink_http | grep NEEDED
```

确认 `file` 显示 ARM、EABI5。HTTP 服务需要 `libsqlite3.so.0`、`libpthread.so.0` 和 C++ 运行库等。本次使用 GCC 8.3 成功构建，GCC 输出了有关 GCC 7.1 参数传递变化的说明，构建正常结束。

在 Ubuntu 将二进制和网页打包，之后用 `exit` 返回 Windows：

```bash
cd ~/edgelink-stage5-repro
tar -cf arm-bundle.tar web \
  -C arm-http/src/http edgelink_http \
  -C ../storage edgelink_db device_store_check \
  -C ../../../arm-mqtt/src/mqtt edgelink_mqtt mqtt_state_check mqtt_client_check
exit
```

## 4. 上传并检查开发板

在 Windows PowerShell 执行。下载和上传均通过 Windows 中转：

```powershell
$cfg = Join-Path $env:USERPROFILE '.ssh\ssh-devices\config'
$bundle = Join-Path $env:TEMP 'edgelink-stage5-repro-arm.tar'
scp -F $cfg 'zm-tailscale:edgelink-stage5-repro/arm-bundle.tar' $bundle
if ($LASTEXITCODE -ne 0) { throw '下载 ARM 产物失败' }
ssh -F $cfg -T debian-board-tailscale 'test ! -e ~/edgelink-stage5-repro && mkdir ~/edgelink-stage5-repro'
if ($LASTEXITCODE -ne 0) { throw '板端目录已存在或创建失败，请选用新目录' }
scp -F $cfg $bundle 'debian-board-tailscale:edgelink-stage5-repro/arm-bundle.tar'
if ($LASTEXITCODE -ne 0) { throw '上传 ARM 产物失败' }
ssh -F $cfg debian-board-tailscale
```

在开发板执行：

```bash
cd ~/edgelink-stage5-repro
tar -xf arm-bundle.tar
ldd ./edgelink_http
ldd ./edgelink_mqtt
./device_store_check
./mqtt_state_check
./mqtt_client_check
```

`ldd` 的依赖应均能解析，三个检查程序应显示通过。`edgelink_http` 的参数为 `DATABASE WEB_DIRECTORY PORT [BIND_ADDRESS]`，默认只监听板端 `127.0.0.1`。它以只读模式打开已经初始化的数据库，所以先启动带 `--db` 的 MQTT 网关。

## 5. 启动测试服务

以下命令在刚才登录的开发板终端执行，创建一个新的测试数据库。先检查端口是否空闲，避免和其他程序冲突：

```bash
ss -lnt | grep -E ':18884 |:18080 ' || echo '测试端口未被监听'
```

若有输出显示端口已被使用，先确认占用者，再选择空闲端口。选择其他端口时，必须同步修改下面服务命令及第 6 节的转发目标。端口空闲后执行：

```bash
cd ~/edgelink-stage5-repro
mkdir run
RUN="$PWD/run"
printf '%s\n' 'listener 18884 127.0.0.1' 'allow_anonymous true' > "$RUN/broker.conf"

/usr/sbin/mosquitto -c "$RUN/broker.conf" > "$RUN/broker.log" 2>&1 &
BROKER_PID=$!
sleep 1
./edgelink_mqtt 127.0.0.1 18884 --db "$RUN/stage5.sqlite3" > "$RUN/mqtt.log" 2>&1 &
MQTT_PID=$!
sleep 2
./edgelink_http "$RUN/stage5.sqlite3" "$PWD/web" 18080 > "$RUN/http.log" 2>&1 &
HTTP_PID=$!
sleep 2
kill -0 "$BROKER_PID" "$MQTT_PID" "$HTTP_PID" && echo '三个服务已启动'
cat "$RUN/mqtt.log"
cat "$RUN/http.log"
```

MQTT 日志应出现订阅就绪信息，HTTP 日志应显示监听地址。保持这个开发板终端打开，以便结束时使用保存的进程编号停止服务。

## 6. 建立转发并运行模拟器

在 Windows 新开 PowerShell 窗口。这个项目的 SSH 专用配置含 `ClearAllForwardings yes`，因此转发命令必须在命令行显式设置 `ClearAllForwardings=no`。下面将 Windows 本机 `18081` 和 `18885` 分别转发到板端 `18080` 和 `18884`：

```powershell
$cfg = Join-Path $env:USERPROFILE '.ssh\ssh-devices\config'
ssh -F $cfg -o ClearAllForwardings=no -o ExitOnForwardFailure=yes -N `
  -L 127.0.0.1:18081:127.0.0.1:18080 `
  -L 127.0.0.1:18885:127.0.0.1:18884 `
  debian-board-tailscale
```

命令正常运行时保持窗口打开。若提示端口已占用，可修改 `-L` 中的 Windows 本地端口，并同步修改浏览器地址或模拟器参数。

另开一个 Windows PowerShell，按 [模拟器说明](../../pc/sensor_sim/README.md)准备依赖后运行：

```powershell
cd E:\Linux_project\demo1
python pc\sensor_sim\sensor_sim.py --host 127.0.0.1 --port 18885 --device-id stage5-repro-01 --interval 5
```

模拟器持续输出遥测后，在浏览器打开 `http://127.0.0.1:18081/`。首页应显示 `stage5-repro-01` 在线、最新温湿度和采样时间。点击设备进入详情页，选择覆盖当前北京时间的范围查询，应出现记录数和温度、湿度曲线。页面每 10 秒刷新设备状态。

也可在另一个 PowerShell 窗口检查 API：

```powershell
Invoke-RestMethod 'http://127.0.0.1:18081/api/devices'
Invoke-RestMethod 'http://127.0.0.1:18081/api/devices/stage5-repro-01/status'
```

按 `Ctrl+C` 结束模拟器后，等待页面刷新，应显示离线，历史记录仍可查询。空时间范围应显示没有遥测记录，查询超过 1000 条时页面提示缩小范围。

## 7. 结束测试与排查

模拟器退出后，在 SSH 转发窗口按 `Ctrl+C`。在仍打开的开发板终端执行：

```bash
kill "$HTTP_PID" "$MQTT_PID" "$BROKER_PID"
exit
```

测试目录保留数据库和日志。若服务没有正常启动，检查 `run/broker.log`、`run/mqtt.log` 和 `run/http.log`。常见排查方向如下：

- 转发窗口报端口占用时，检查 Windows 本机端口，并确认 `ClearAllForwardings=no` 已加入命令。
- HTTP 启动失败时，确认 MQTT 网关已成功创建数据库、路径正确且网页目录存在，再检查 `http.log`。
- 页面没有设备时，确认模拟器连接成功、MQTT 网关订阅就绪、数据库路径一致。
- 曲线为空时，确认设备 ID 和查询时间范围。查询使用北京时间，包含起点、不包含终点。
- 板端运行提示库缺失时，检查 `ldd` 结果和阶段四记录的目标架构依赖。

## 8. 本次实测记录

| 检查              | 2026-11-08 结果                                                                       |
| --------------- | ----------------------------------------------------------------------------------- |
| Ubuntu ARM 构建   | GCC 8.3 构建 HTTP、SQLite 工具及独立 MQTT 网关成功。HTTP 程序为 ARM EABI5。                          |
| 板端库与检查程序        | HTTP 的 `ldd` 依赖均找到。`device_store_check`、`mqtt_state_check`、`mqtt_client_check` 均通过。 |
| Tailscale 与数据链路 | Windows 模拟器经 SSH 转发发送遥测，HTTP 读到设备在线状态及最新温湿度。                                        |
| 查询与并发写入         | 验证详情、状态、历史截断、非法参数 400、设备不存在 404。连续查询期间，历史记录从 65 条增至 71 条。                           |
| 浏览器             | 设备列表、详情和 74 条历史记录的温湿度曲线已显示。用户随后按步骤验收，反馈没有问题。小窗口中曲线坐标文字偏小。                           |

本次实际构建目录为 Ubuntu `~/edgelink-stage5-work`，板端产物为 `~/edgelink-stage5-test`，联调数据库与日志为其下的 `run.RrZizd`。本次联调使用的临时服务和 SSH 转发在测试结束后停止。上文的 `edgelink-stage5-repro` 是供重新操作的独立目录，命令根据实际验证过程整理，重新执行时应按当前环境检查路径与端口。
