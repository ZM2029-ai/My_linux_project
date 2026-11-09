# gateway —— 网关核心（运行在 i.MX6ULL 开发板上）

## 目标环境

| 项目 | 值 |
| --- | --- |
| CPU | i.MX6ULL（ARMv7，单核，792 MHz） |
| 架构 | armhf 硬浮点 |
| 系统 | Debian 10 (buster) |
| glibc | 2.28 |
| 内核 | 4.19.35-imx6 |
| 内存 | 486 MiB（无 Swap） |
| 部署目录 | `/home/debian/edgelink` |

## 构建

**必须在虚拟机里用 GCC 8.3 工具链构建**，不能在 Windows 或板子上构建。

```bash
# 在虚拟机宿主机执行（脚本会进入 chroot 构建）
REPO_DIR=/mnt/hgfs/demo1 ./scripts/build.sh
```

阶段三 MQTT 接收功能需要 chroot 中的 `libmosquitto-dev:armhf` 和 `nlohmann-json3-dev`，可通过以下方式启用（默认关闭）：

```bash
REPO_DIR=/mnt/hgfs/demo1 ENABLE_MQTT=ON ./scripts/build.sh
```

部署后，开发板上运行 `./edgelink --mqtt-host 127.0.0.1`，也可在地址后指定端口。网关解析遥测和状态 JSON，维护设备状态与最新遥测，并在断线后重连、重新订阅。Ctrl+C 停止。独立 MQTT 构建、代码阅读顺序和手动运行步骤见 [MQTT 模块说明](src/mqtt/README.md)。当前代码尚未通过板端验收。音乐使用原有 `--music-port PORT` 运行。

构建脚本做的事：bind mount 源码/构建目录 → chroot 内 `cmake` + `cmake --build` → 取回产物并显示符号版本自检。

## 阶段四 SQLite 存储

在 Debian 10 chroot 中准备 `libsqlite3-dev:armhf`，板端使用 `libsqlite3-0`。构建完整网关时，在虚拟机宿主机执行：

```bash
REPO_DIR=/mnt/hgfs/demo1 ENABLE_MQTT=ON ENABLE_SQLITE=ON ./scripts/build.sh
```

产物为 `~/edgelink-bin/edgelink` 和 `~/edgelink-bin/edgelink_db`，查询工具需要单独传到板上。使用手动 CMake 构建时，添加 `-DEDGELINK_ENABLE_SQLITE=ON`。也可搭配 `-DEDGELINK_MQTT_ONLY=ON` 构建独立 MQTT 网关及查询工具。单独检查存储层可使用 `-DEDGELINK_STORAGE_ONLY=ON -DEDGELINK_BUILD_STORAGE_CHECKS=ON`，本机原生构建后用 CTest 运行，ARM 检查程序在板上运行。

在板上从部署目录启动网关，数据库应放在有写权限的本地目录。每个数据库由一个网关实例维护：

```bash
./edgelink --mqtt-host 127.0.0.1 1883 --db ./edgelink.sqlite3
# 独立 MQTT 程序使用 ./edgelink_mqtt 127.0.0.1 1883 --db ./edgelink.sqlite3
```

网关首次打开数据库时建表，收到合法消息后自动注册设备。省略 `--db` 时使用阶段三的内存模式。在另一终端查询或更新同一个数据库：

```bash
./edgelink_db ./edgelink.sqlite3 list
./edgelink_db ./edgelink.sqlite3 show sensor-01
./edgelink_db ./edgelink.sqlite3 rename sensor-01 'Lab sensor'
./edgelink_db ./edgelink.sqlite3 history sensor-01 '2026-11-06 00:00:00' '2026-11-07 00:00:00' 100
```

历史按北京时间的采样时间过滤，含开始时间，到结束时间之前截止。查询默认返回 100 条，上限 1000 条，同秒记录按写入顺序排列。重启保留设备资料、遥测和操作记录，当前在线状态由本次会话重新确认。写入失败会记录原因，失败的遥测样本会丢失。网关会继续处理后续上报，并重试同步已注册设备的当前状态。

启用阶段六 HTTP 控制时，增加 `-DEDGELINK_ENABLE_CONTROL=ON`，并为 `edgelink_http` 配置只允许所有者读取的账号文件和浏览器 Origin。接口、启动参数和账号文件格式见 [HTTP API 约定](../protocol/http-api.md)。控制服务运行在 HTTP 进程中，通过 MQTT 发布启停命令并记录回执审计。

## 手动构建（不经脚本）

```bash
sudo mount --bind <repo> /opt/buster/work
sudo mount --bind ~/build/edgelink /opt/buster/build
sudo chroot /opt/buster /bin/bash -c "
  cmake -S /work/gateway -B /build \
        -DCMAKE_TOOLCHAIN_FILE=/work/gateway/cmake/toolchain-arm-linux-gnueabihf.cmake \
        -DCMAKE_BUILD_TYPE=Release
  cmake --build /build -- -j2
"
```

## 目录约定

**一个功能一个文件夹**，在实现该功能时创建：

```
src/
├── main.cpp          # 入口：初始化 → 加载配置 → 启动各模块
├── log/              # 日志模块（阶段一任务 5）
├── config/           # 配置模块（阶段一任务 5）
├── net/              # TCP 通信（阶段二）
├── audio/            # MP3 解码 + ALSA 输出（阶段二）
├── mqtt/             # MQTT 通信（阶段三）
├── storage/          # SQLite 存储（阶段四）
└── http/             # HTTP API（阶段五）
```

头文件与该功能实现放在同一文件夹（`src/log/log.hpp` + `src/log/log.cpp`），不设全局 `include/`；等真的出现跨功能公共头再单独建。

## 代码约束

1. **C++14 基线**，可用 C++17 但避开 `std::filesystem`（GCC 8 无完整实现）。
2. 不引入需要复杂交叉编译过程的第三方库；优先用板上 apt 提供的版本（SQLite 3.27.2、Mosquitto 1.5.7）。
3. 每加一个功能，必须能在板上跑起来并留下日志。
4. 资源预算按**单核 792 MHz、486 MB 内存**设计，避免大缓冲区与忙等。

## 部署

```bash
./scripts/deploy.sh
```

产物路径：本机 `~/edgelink-bin/edgelink` → 板上 `/home/debian/edgelink/edgelink`。

## 当前状态

- [x] CMake 工程骨架
- [x] 交叉编译工具链配置（GCC 8.3）
- [x] 启动横幅 / 环境自检
- [ ] 日志模块（阶段一任务 5）
- [ ] 配置模块（阶段一任务 5）
