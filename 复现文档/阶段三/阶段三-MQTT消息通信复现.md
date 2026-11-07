# 阶段三 MQTT 消息通信复现报告

板端联调日期为 2026 年 11 月 5 日。阶段三完成了 Windows 模拟设备向 i.MX6ULL 网关上报温湿度和在线状态的链路，并验证了多设备、异常离线、无效数据和 Broker 重启后的恢复。完整网关也通过了 MQTT 接收和 TCP 音乐的程序回归。音乐测试记录了板端播放完成响应，现场听感没有独立记录。

## 1. 系统分工

```text
Windows 模拟设备（Python + Paho）
       │ MQTT 3.1.1，经 TCP 连接开发板的 1884 端口
       ▼
i.MX6ULL 上的 Mosquitto Broker
       │ 按主题转发，C++ 网关通过 127.0.0.1:1884 订阅
       ▼
i.MX6ULL 上的 C++ 网关（libmosquitto + JSON 解析 + 设备状态缓存）

Ubuntu 虚拟机负责把源码编译成 ARM 程序，再用 SCP 传给开发板。
```

发布者把消息交给 Broker，Broker 按主题转发给订阅者。两个模拟器各用一个设备 ID，网关订阅 `edgelink/device/+/telemetry` 和 `edgelink/device/+/status`，其中 `+` 匹配一层设备 ID。温湿度和时间戳用 UTF-8 JSON 表示，网关收到后解析、校验，并在内存中维护最新数据和设备状态。[OASIS MQTT 3.1.1 规范](https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html) · [项目 MQTT 主题](../../protocol/mqtt-topics.md) · [遥测格式](../../protocol/device-telemetry.md)

| 机制 | 本项目如何使用 |
| --- | --- |
| QoS | 遥测用 QoS 0，允许偶尔丢失一条读数。状态用 QoS 1，发布者可得到确认，接收端仍可能收到重复消息。 |
| Retain，保留消息 | Broker 为状态主题保存最近一条保留消息，供新订阅者读取。重放的 `online` 只表示历史报告，网关等实时心跳或遥测到来后才确认设备在线。 |
| Last Will，遗嘱 | 模拟器连接时登记一条 `offline` 状态。Broker 检测到异常断开时发布它。正常退出时，模拟器自己发布 `offline` 并等待确认。 |
| SUBACK，订阅确认 | 网关收到两条订阅的成功确认后输出 `ready`。Broker 重启后，网关重新连接并重新订阅。 |
| 心跳与状态超时 | 模拟器约每 10 秒发送在线心跳。网关用单调时钟判断，设备在线后若约 60 秒没有实时消息，或网关失去 Broker 连接，状态转为 `unknown`。 |

阶段四负责 SQLite 存储，阶段五负责通过 HTTP 向 PC 页面提供查询，阶段六接入 MQTT 命令下发和响应。阶段三验证的是设备上报及网关接收。

### 1.1 新技术：从一条数据看 MQTT

MQTT 是基于发布和订阅的消息协议。本项目中，Windows 模拟器是**发布者**，开发板上的 Mosquitto 是负责接收与转发消息的 **Broker**，C++ 网关是**订阅者**。发布者只需要指定主题，Broker 将消息转发给订阅了匹配主题的客户端；模拟器不需要知道网关的 IP 或直接连接网关。

例如 `sensor-01` 生成一条温湿度数据后，向 `edgelink/device/sensor-01/telemetry` 发布 UTF-8 JSON；网关订阅 `edgelink/device/+/telemetry`，其中 `+` 匹配一层名称，所以也能接收 `sensor-02` 的消息。网关根据主题提取设备 ID，解析并校验载荷，更新内存中的最新读数。这里的主题是 MQTT 的消息分类路径，不是文件系统目录；MQTT 通信走 Broker 的 TCP 连接，但消息主题和传递语义由 MQTT 协议定义。

QoS 0 表示最多交付一次，断线等情况下可能丢失；QoS 1 使用确认和重传，可能产生重复消息。**QoS 1 的协议确认不等于业务已经处理成功**。本项目用 QoS 0 发送遥测、QoS 1 发送设备状态；保留消息、遗嘱、订阅确认和在线超时的实际规则见上表与[主题约定](../../protocol/mqtt-topics.md)。

### 1.2 实际使用的库、接口与运行位置

| 技术或接口 | 用在何处 | 本项目的实际作用 |
| --- | --- | --- |
| Mosquitto Broker、`mosquitto_pub` | 开发板 | Broker 转发主题消息；`mosquitto_pub` 用于手动注入消息，验证保留状态、超时和非法载荷。 |
| Eclipse Paho MQTT Python：`Client`、`will_set`、`connect_async`、`loop_start`、`publish` | Windows 模拟器 | 创建 MQTT 3.1.1 客户端、登记离线遗嘱、异步连接并启动网络线程，发布遥测与状态；主线程按时间安排上报。 |
| libmosquitto：`mosquitto_connect`、`mosquitto_subscribe`、`mosquitto_loop` 及回调 | 开发板 C++ 网关 | 建连、订阅主题、循环处理网络事件；连接、订阅确认和消息到达时调用对应回调。 |
| Python `json`、C++ `nlohmann/json` | Windows 模拟器、开发板网关 | 模拟器编码 JSON，网关解析并检查字段；JSON 头文件在编译时使用，不新增 JSON 运行时动态库。 |
| 单调时钟：Python `time.monotonic()`、C++ `std::chrono::steady_clock` | 两端进程 | 安排发送间隔和判断在线超时，避免系统时间调整影响间隔计算；载荷时间戳另用本地时间文本表示。 |

代码阅读顺序：先看 [Windows 模拟器](../../pc/sensor_sim/sensor_sim.py) 的 `SensorSimulator`，再看 [MQTT 客户端](../../gateway/src/mqtt/mqtt_client.cpp) 的回调和网络循环，以及[设备状态缓存](../../gateway/src/mqtt/device_registry.cpp)；JSON 字段与取值以[遥测协议](../../protocol/device-telemetry.md)为准。阶段三缓存保存在内存中，进程退出后不持久化；数据库属于后续阶段。

## 2. 阶段三构建依赖

本阶段沿用阶段一的 Debian 10 chroot 和 GCC 8.3 交叉编译环境。搭建方法与二进制兼容性见[阶段一复现文档](../阶段一/阶段一-交叉编译环境搭建与验证.md)。下表只列出本阶段新增或与完整网关构建直接相关的依赖。

| chroot 内的组件 | 版本或包 | 在构建中的工作 |
| --- | --- | --- |
| MQTT 开发库 | `libmosquitto-dev:armhf`，`1.5.7-1+deb10u1` | 提供 `mosquitto.h` 和 ARM 链接库。板上通过 `libmosquitto1:armhf` 加载 `libmosquitto.so.1`。 |
| JSON 解析库 | `nlohmann-json3-dev`，`3.5.0-0.1` | C++ 编译时使用头文件解析 JSON，部署时没有额外的 JSON 动态库。 |
| 阶段二音频开发库 | `libmpg123-dev:armhf` 与 `libasound2-dev:armhf` | 构建完整网关的 MP3 解码和 ALSA 播放模块。独立 MQTT 目标无需这两个包。 |

本阶段构建静态库时还用到 GNU Binutils 的 `ar`（打包目标文件）和 `ranlib`（建立归档符号索引）；工具链文件显式指定了它们的 ARM 版本路径。未找到工具时的实际报错与处理见第 5 节。[项目工具链文件](../../gateway/cmake/toolchain-arm-linux-gnueabihf.cmake)

chroot 外的组件分别是 Windows 上运行模拟器的 Python 和 Eclipse Paho MQTT 2.x，板上运行的 Mosquitto Broker 1.5.7、`mosquitto-clients` 与 libmosquitto 运行库，以及在两台 Linux 设备之间传送产物的 SSH/SCP。本机无网络测试使用过 Paho 2.1.0，依赖文件允许 Paho 2.x。[Eclipse Paho Python 客户端](https://eclipse.dev/paho/clients/python/) · [模拟器依赖](../../pc/sensor_sim/requirements.txt)

## 3. 按实测方式交叉编译与部署

### 3.1 准备工具和源码

先在 Ubuntu 虚拟机按阶段一准备 `/opt/buster`。本次检查显示 `armhf` 已启用，ARM 的 MQTT 开发包已安装，补装的是 JSON 头文件。以下命令在 **Ubuntu 虚拟机**执行。第一次准备完整网关依赖时，也安装阶段二的 ARM 音频开发包。

```bash
CHROOT_DIR=/opt/buster
sudo chroot "$CHROOT_DIR" dpkg --print-architecture
sudo chroot "$CHROOT_DIR" dpkg --print-foreign-architectures
# 若上一条没有列出 armhf，先执行下面这条，再刷新软件包索引
# sudo chroot "$CHROOT_DIR" dpkg --add-architecture armhf
sudo chroot "$CHROOT_DIR" apt-get update
sudo chroot "$CHROOT_DIR" apt-cache policy libmosquitto-dev:armhf nlohmann-json3-dev
sudo chroot "$CHROOT_DIR" apt-get install libmosquitto-dev:armhf nlohmann-json3-dev
sudo chroot "$CHROOT_DIR" apt-get install libmpg123-dev:armhf libasound2-dev:armhf
```

本次联调用独立源码快照解决共享目录版本不一致。复现这条路径时，先在 **Windows 项目根目录**打包当前网关源码并传给虚拟机。IP 使用当时实测值，地址变化后按设备本机的 `ip -4 -br addr` 替换。

```powershell
$bundle = Join-Path $env:TEMP 'edgelink-stage3-qa-source.tar.gz'
tar.exe -czf $bundle gateway
scp $bundle zm@10.229.71.58:edgelink-stage3-qa-source.tar.gz
```

在 **Ubuntu 虚拟机**解包到 chroot 内的独立目录，并核对工具链源码。以下命令可在同一个普通终端依次执行。

```bash
CHROOT_DIR=/opt/buster
QA_DIR=$(sudo mktemp -d "$CHROOT_DIR/stage3-qa.XXXXXX")
sudo chmod 755 "$QA_DIR"
sudo tar -xzf "$HOME/edgelink-stage3-qa-source.tar.gz" -C "$QA_DIR"
INNER_DIR="/$(basename "$QA_DIR")"
grep -nE 'CMAKE_(AR|RANLIB)' "$QA_DIR/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake"
```

本次实际目录是 Ubuntu 侧的 `/opt/buster/stage3-qa.kgkTiR`，在 chroot 内则是 `/stage3-qa.kgkTiR`。如果确认 VMware 共享目录内容是最新源码，也可以按操作手册把 `/mnt/hgfs/share_for_vmware/demo1` 挂载到 Ubuntu 侧的 `/opt/buster/work`，进入 chroot 后用 `/work/gateway` 构建。[操作手册](../../操作手册.md)

### 3.2 构建独立版和完整版

```bash
TOOLCHAIN="$INNER_DIR/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake"

sudo chroot "$CHROOT_DIR" cmake -S "$INNER_DIR/gateway" -B "$INNER_DIR/build-mqtt" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DEDGELINK_MQTT_ONLY=ON -DEDGELINK_BUILD_MQTT_CHECKS=ON
sudo chroot "$CHROOT_DIR" cmake --build "$INNER_DIR/build-mqtt" -- -j2

sudo chroot "$CHROOT_DIR" cmake -S "$INNER_DIR/gateway" -B "$INNER_DIR/build-full" \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DEDGELINK_ENABLE_MQTT=ON
sudo chroot "$CHROOT_DIR" cmake --build "$INNER_DIR/build-full" -- -j2

ARTIFACTS="$QA_DIR/build-mqtt/src/mqtt"
file "$ARTIFACTS/edgelink_mqtt" "$QA_DIR/build-full/edgelink"
```

`EDGELINK_MQTT_ONLY=ON` 只构建独立 MQTT 入口和启用的检查程序。`EDGELINK_ENABLE_MQTT=ON` 给完整网关接入同一个 MQTT 模块，并保留音乐入口。测试版与完整版使用各自的构建目录，便于独立检查 CMake 配置。实测独立程序为 32 位 ARM EABI5 ELF，解释器是 `/lib/ld-linux-armhf.so.3`。

### 3.3 上传与检查

```bash
BOARD=debian@10.229.71.142
ssh "$BOARD" 'mkdir -p ~/edgelink-stage3'
scp "$ARTIFACTS/edgelink_mqtt" "$ARTIFACTS/mqtt_state_check" \
  "$ARTIFACTS/mqtt_client_check" "$QA_DIR/gateway/config/mosquitto-stage3.conf" \
  "$BOARD:edgelink-stage3/"
scp "$QA_DIR/build-full/edgelink" "$BOARD:edgelink-stage3/edgelink_full"
```

在 **开发板**检查程序和依赖。板上实测已有 `mosquitto`、`mosquitto-clients` 和 `libmosquitto1`，三者都是 `1.5.7-1+deb10u1`。若复现环境缺包，可在板上用 APT 安装。

```bash
cd ~/edgelink-stage3
dpkg-query -W mosquitto mosquitto-clients libmosquitto1
ldd ./edgelink_mqtt
./mqtt_state_check
./mqtt_client_check
```

`ldd` 应解析到 ARM 的 `libmosquitto.so.1`，两个检查程序应打印 `passed`。第一个检查 JSON 和设备状态规则，第二个模拟连接、订阅失败及恢复，真实联网验证见下一节。

## 4. 三端联调与结果

在两个 SSH 窗口登录**同一块开发板**。终端 A 跑测试 Broker，终端 B 跑网关。在 A 先检查 `ss -ltn`，确认 `1884` 空闲。本次板上现有 Broker 已监听 `1883`，所以测试实例使用局域网端口 `1884`。

```bash
# 开发板终端 A
cd ~/edgelink-stage3
sed 's/^listener 1883$/listener 1884/' mosquitto-stage3.conf > broker-test.conf
mosquitto -c broker-test.conf -v
```

```bash
# 开发板终端 B
cd ~/edgelink-stage3
./edgelink_mqtt 127.0.0.1 1884
```

在 **Windows PowerShell** 运行模拟器和无网络检查。再开一个 PowerShell，用相同的命令启动第二个实例，把 ID 改为 `sensor-02`，上报间隔改为 3 秒。

```powershell
Set-Location 'E:\Linux_project\demo1\pc\sensor_sim'
python -m pip install -r requirements.txt
python -m unittest test_sensor_sim.py
python sensor_sim.py --host 10.229.71.142 --port 1884 --device-id sensor-01 --interval 5
```

下表记录了真实结果。正常退出用 Ctrl+C，异常退出强制结束专用模拟器进程。保留状态、超时和非法消息用板上的 `mosquitto_pub` 注入测试消息，具体载荷和命令见操作手册第 4、5 节。Broker 重启用终端 A 的同一配置，网关和 `sensor-01` 继续运行。[操作手册](../../操作手册.md)

| 测试 | 观察结果 | 记录 |
| --- | --- | --- |
| ARM 产物、依赖和检查程序 | 独立版与完整版构建通过，板上 `ldd` 没有缺失项，C++ 两项和 Windows Python 两项通过 | 构建输出与板端检查输出 |
| 两个设备同时上报 | 网关 `ready`，分别收到 `sensor-01` 和 `sensor-02` 的温湿度及在线状态 | `gateway.log` |
| 正常退出 | `sensor-01` 变为 `offline`，Broker 日志有正常 `DISCONNECT`，`sensor-02` 继续上报 | `gateway.log`、`broker.log` |
| 异常退出 | 强制终止 `sensor-02` 后，网关收到其离线遗嘱 | `gateway.log`、`broker.log` |
| 保留状态和超时 | 网关重启后收到两个保留的离线状态。保留在线报告显示 `reported=online retained=1 state=unknown`。单次实时上线的测试设备静默 60 秒后转为 `unknown` | `gateway-restart.log` |
| 非法数据 | 错误 JSON 与湿度 101 分别被拒绝，合法遥测继续到达 | `gateway-restart.log` |
| Broker 重启 | 网关报告掉线，设备变为 `unknown`。Broker 恢复后，网关重新订阅并进入 `ready`，模拟器自动上线，20:41:58 的遥测再次到达 | `gateway-restart.log`、`broker-restart.log` |
| 完整网关回归 | `edgelink_full --mqtt-host 127.0.0.1 1884` 收到实时遥测。切换为 `--music-port 9000` 后，Windows 发送 32,600 字节测试 MP3，板端打印 `playback complete`，发送端收到成功响应 | `full-mqtt.log`、`music.log` |

当时的测试程序在板端 `/home/debian/edgelink-stage3/`，联调日志在 `/home/debian/edgelink-stage3/run.lYDv3k/`。测试进程在结束后退出，检查确认 `1884` 和 `9000` 不再监听，系统 Broker 仍监听 `1883`。

## 5. 实际遇到的困难和处理

| 现象 | 定位过程与处理 |
| --- | --- |
| 在 Ubuntu 直接安装 `libmosquitto-dev:armhf` 时提示无法定位软件包 | 安装位置要对应构建环境。进入 Debian 10 chroot 后检查 `dpkg --print-foreign-architectures` 和 `apt-cache policy`。本次 chroot 已有 ARM Mosquitto 开发包，补装 `nlohmann-json3-dev` 后进入 CMake。 |
| `findmnt -M /opt/buster/work` 没输出，目录里只有阶段一的 `hello.cpp` 与 `hello_arm8` | 在 Ubuntu 侧先用 `ls` 查真实共享路径 `/mnt/hgfs/share_for_vmware/demo1`，再用 `mount --bind` 将它映射到 chroot 的 `/work`。用 `findmnt` 确认挂载来源，并在 chroot 内检查 `/work/gateway/src/mqtt/mqtt_main.cpp`。 |
| 上传时 `file` 查到了 `/build-mqtt/...`，SSH 报空主机名 | 当前终端的 `CHROOT_DIR` 和 `BOARD` 没有值。退回 Ubuntu 普通终端，重新设置 `REPO_DIR`、`CHROOT_DIR`、`BOARD`、`ARTIFACTS`，先确认文件存在再 SCP。 |
| JSON 安装出现语言环境和 `/dev/pts` 日志提示 | apt 最终完成包配置，随后 CMake 和 GCC 成功读取 JSON 头文件。以安装退出状态和编译结果确认是否阻塞。 |
| 编译目标文件成功，打包静态库时报 `Error running link command: No such file or directory` | 查看 CMake 的 `link.txt` 发现命令从 `CMAKE_AR-NOTFOUND qc` 开始。CMake 3.13 在这套带 `-8` 后缀的工具链上没找到归档工具，chroot 里实际已有 `arm-linux-gnueabihf-ar` 和 `ranlib`。工具链文件补上 `CMAKE_AR`、`CMAKE_RANLIB` 的绝对路径，再用新构建目录配置。 |
| 修复后虚拟机重建，仍出现同样的归档错误 | 在虚拟机对工具链文件执行 `grep CMAKE_AR`，确认共享目录仍是旧版。将 Windows 工作区的网关源码打包传入 chroot 的独立目录，核对新设置后重新构建。修复生效后，独立版和完整版都生成了 ARM 产物。 |
| 最初用错开发板 IP，SSH 在握手前后失败 | 板上 `ip -4 -br addr` 显示 `wlan0` 为 `10.229.71.142`，`systemctl status ssh` 显示 SSH 已在监听。改用实测地址并核对 SSH 主机指纹，随后成功登录与上传。 |
| 只看到 TCP 音乐发送端的成功提示 | 同时核对了板端 `music.log` 的 `stream started`、`received 32600 bytes` 和 `playback complete`。这次验证覆盖传输和播放程序的完成路径，现场听感以接入耳机或扬声器的试听结果为准。 |

阅读代码可从模拟器的发布逻辑入手，再看网关的连接与订阅、消息解析和状态缓存。模块说明中列出了对应文件和独立构建入口。[MQTT 模块说明](../../gateway/src/mqtt/README.md)
