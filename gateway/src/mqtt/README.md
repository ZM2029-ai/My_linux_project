# 阶段三 MQTT 代码阅读与运行

当前提供完整的阶段三代码。硬件交叉编译和板端联调尚未验收，本说明是代码使用说明，不是已通过的复现报告。

## 阅读顺序

1. `../../../../protocol/mqtt-topics.md` 与 `device-telemetry.md`：先理解主题和消息字段。
2. `../../../../pc/sensor_sim/sensor_sim.py`：模拟设备如何连接、上报、发送心跳和退出。
3. `mqtt_client.hpp` / `.cpp`：libmosquitto 连接、订阅、收包及重连循环。
4. `device_message.hpp` / `.cpp`：把主题和 JSON 变成经过校验的 `DeviceMessage`。
5. `device_registry.hpp` / `.cpp`：维护各设备的最新遥测和 online/offline/unknown 状态。
6. `mqtt_gateway.cpp`：把上述模块组装起来，并输出日志。
7. `mqtt_main.cpp`：独立程序入口；原网关 `../main.cpp` 也调用同一个组装函数。

```text
模拟器 --MQTT--> Broker --订阅--> mqtt_client
                                      |
                                      v
                                mqtt_gateway
                                      |
                            parseDeviceMessage
                                      |
                                DeviceRegistry
                                      |
                         日志 / snapshot()供后续业务使用
```

连接状态为 `connecting → subscribing → ready`；两条订阅均收到成功 SUBACK 才进入 ready。连接断开、连接拒绝、订阅失败或握手超过 10 秒，进入 disconnected，等待 3 秒后重试并重新订阅。10 秒计时从 TCP 建连返回后开始；DNS/TCP 建连使用系统超时，期间退出可能延迟。

设备状态与网关连接状态分别维护。实时消息确认 online，设备离线消息确认 offline；网关掉线或 60 秒无实时心跳/遥测时变成 unknown。最新遥测在内存中保留，是否新鲜应结合时间与 presence 判断。状态变化规则详见协议文档。缓存上限 256 个设备，进程重启后清空。

所有网关回调都在单一事件循环线程执行，回调应快速返回。`DeviceRegistry` 不提供跨线程同步；后续数据库或 HTTP 模块应通过明确的线程同步或消息队列访问快照。阶段三没有数据库、HTTP 或控制执行逻辑。

## 编译依赖

在 Ubuntu 虚拟机上进入现有 Debian 10 chroot，在其中安装目标架构客户端开发库和 JSON 头文件库：

```bash
sudo chroot /opt/buster /bin/bash
apt-get update
apt-get install libmosquitto-dev:armhf nlohmann-json3-dev
```

前提是该 chroot 已启用 armhf 架构及对应软件源。`nlohmann-json3-dev` 只提供编译时头文件，不给板端增加 JSON 动态库依赖。代码使用兼容 JSON 3.5.0、libmosquitto 1.5.7 的接口。板端需要 `libmosquitto1`；Broker 使用 `mosquitto`，手动测试工具使用 `mosquitto-clients`。

## 只编译 MQTT

沿用项目原有源码挂载 `/work`，在 chroot 内执行（构建目录采用独立的 `/build-mqtt`）：

```bash
cmake -S /work/gateway -B /build-mqtt \
  -DCMAKE_TOOLCHAIN_FILE=/work/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake \
  -DEDGELINK_MQTT_ONLY=ON -DEDGELINK_BUILD_MQTT_CHECKS=ON
cmake --build /build-mqtt -- -j2
```

产物为 `/build-mqtt/src/mqtt/edgelink_mqtt` 和同目录的 `mqtt_state_check`、`mqtt_client_check`。这些是 ARM 程序，需要部署到板上执行；不要在 amd64 chroot 中直接运行。独立 MQTT 构建不依赖 mpg123 或 ALSA。

在普通 Linux 主机本地构建时，安装本机架构的 `libmosquitto-dev` 与 `nlohmann-json3-dev`，省略工具链参数即可。原有完整网关可通过 `ENABLE_MQTT=ON ./scripts/build.sh` 构建；此方式仍需原有音频依赖。

## 运行链路

Broker 与网关均运行在开发板，模拟器运行在 Windows。以下命令供后续手动联调，当前未执行板端验证。

1. 板上确认系统 Mosquitto 服务已运行。若选择手动启动，可使用仓库中的 `gateway/config/mosquitto-stage3.conf`：`mosquitto -c <配置文件路径>`。系统服务已占用端口时不要再在同一端口启动第二个 Broker。配置文件是无认证的局域网学习配置，不会自动覆盖系统配置。
2. 板上启动独立网关：`./edgelink_mqtt 127.0.0.1 1883`。完整网关的等价入口为 `./edgelink --mqtt-host 127.0.0.1 1883`。
3. Windows 在 `pc/sensor_sim` 中执行 `python -m pip install -r requirements.txt`。
4. 启动设备：`python sensor_sim.py --host <开发板IP> --device-id sensor-01 --interval 5`。第二个终端使用 `sensor-02`，即可观察多个设备。
5. Ctrl+C 结束模拟器时，发布 offline 并最多等待 2 秒确认；异常结束进程时，由 Broker 检测连接断开后发送遗嘱。网关日志应区分 `reported`（消息报告）与 `state`（当前确认状态）。

## 检查与后续验收

`mqtt_state_check` 检查消息校验、历史保留状态、多设备上限、在线确认与超时、网关掉线后的状态失效，不依赖 Broker。`mqtt_client_check` 使用真实客户端头文件和网络函数替身，检查连接失败、CONNECT 拒绝、SUBACK 拒绝、连接丢失后的重新订阅及退出清理，不链接真实客户端库、不连接 Broker，耗时约 12 秒。在普通 Linux 主机本地构建后，可在构建目录运行 `ctest --output-on-failure`；交叉构建的检查程序应在板上运行。

Python 检查可在模拟器目录执行 `python -m unittest test_sensor_sim.py`，其网络方法被替换，不连接 Broker。

2026-11-04 本地验证：Windows/MinGW 的 C++14 构建通过，独立程序链接并加载了从官方源码构建的 libmosquitto 1.5.7，JSON 使用官方 3.5.0 头文件；C++ 两项检查和 Python/Paho 2.1.0 两项检查通过。该临时 Windows 库关闭 TLS 和线程，仅供本次同步接收程序构建检查，不用于 ARM 部署；没有测试 Broker 收发或开发板。项目正式流程仍为 Debian 10/GCC 8.3 交叉构建。

后续板端仍需逐项验证：正常与多设备上报、正常退出、异常断开、Broker 重启、连接恢复与重新订阅，以及原音乐入口的回归。只有通过这些检查，才能认为阶段三验收完成。
