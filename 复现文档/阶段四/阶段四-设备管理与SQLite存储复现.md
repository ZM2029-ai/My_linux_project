# 阶段四：设备管理与 SQLite 存储复现

本阶段通过 MQTT 接收模拟设备数据，将设备信息、状态和遥测保存到 SQLite，并在开发板上按设备和时间查询历史。2026-11-07 已完成 ARM 交叉编译和板端联调，实测结果见本文第 6 节。

## 1. 数据如何流动

```text
Windows 模拟器 → MQTT Broker → 板端 MQTT 网关 → SQLite 数据库文件
                                                    ↑
                                         edgelink_db 查询工具
```

SQL 是操作数据的语句，SQLite 是执行 SQL 并管理数据库文件的库。程序首次使用指定路径时创建数据库和表。

| 表 | 用途 |
| --- | --- |
| `devices` | 保存设备 ID、名称、注册时间、最近接收时间和状态。 |
| `telemetry` | 保存设备采样时间、网关接收时间、温度和湿度。 |
| `operation_log` | 保存注册、改名和状态变化记录。 |

设备第一次发送合法消息时自动注册。历史查询按设备采样时间筛选，时间格式为北京时间 `YYYY-MM-DD HH:MM:SS`，范围包含起点、排除终点。每次查询默认返回 100 条，最多返回 1000 条。

## 2. 环境和依赖

| 位置 | 依赖与用途 |
| --- | --- |
| Windows | Python 和模拟器依赖 `pc/sensor_sim/requirements.txt`，运行两个 MQTT 模拟设备。 |
| Ubuntu 虚拟机 | 阶段一准备的 `/opt/buster` Debian 10 环境，包含 GCC 8.3 ARM 交叉编译器、Mosquitto ARM 开发库和 JSON 头文件。 |
| i.MX6ULL 开发板 | Debian 10 armhf、`libsqlite3-0`、`libmosquitto1`、`mosquitto` 和 `mosquitto-clients`。完整网关还需要音频运行库。 |

板端确认软件包，在开发板终端执行：

```bash
dpkg-query -W libsqlite3-0 libmosquitto1 mosquitto mosquitto-clients
```

本次板端 `libsqlite3-0:armhf` 版本为 `3.27.2-3+deb10u1`。虚拟机交叉编译需要相同目标架构的 SQLite 头文件与链接库。SQL 实现在 `gateway/src/storage/device_store.cpp`，C++ 业务层调用 `DeviceStore` 完成读写。

## 3. 交叉编译

### 3.1 本次实测环境

2026-11-07 的源码快照和构建目录为 Ubuntu 虚拟机上的 `/home/zm/edgelink-stage4-resume.FUvngC`。它沿用了 `/opt/buster` 中的 GCC 8.3 和目标库。由于普通 SSH 会话不能免密执行 `sudo chroot`，本次从 Ubuntu 用户会话调用该编译器，并用临时工具链文件指向 `/opt/buster`。SQLite armhf 包解压在独立目录，版本与板上运行库一致。

以下是实测构建所用的核心命令。先将当前工作区的 `gateway` 目录复制到 Ubuntu 的独立目录，再在 **Ubuntu 虚拟机**进入该目录执行。若使用新的目录，`BASE` 指向已准备好的 SQLite 包、宿主兼容库及工具链文件所在目录。

```bash
BASE=/home/zm/edgelink-stage4.PRyv72
cd /home/zm/edgelink-stage4-resume.FUvngC
export LD_LIBRARY_PATH="$BASE/host-compat"

cmake -S gateway -B arm-mqtt \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/relocated-arm.cmake" \
  -DEDGELINK_ENABLE_MQTT=ON -DEDGELINK_MQTT_ONLY=ON \
  -DEDGELINK_ENABLE_SQLITE=ON \
  -DEDGELINK_BUILD_MQTT_CHECKS=ON -DEDGELINK_BUILD_STORAGE_CHECKS=ON \
  -DSQLite3_INCLUDE_DIR="$BASE/sqlite-armhf/usr/include" \
  -DSQLite3_LIBRARY="$BASE/sqlite-armhf/usr/lib/arm-linux-gnueabihf/libsqlite3.so" \
  -DCMAKE_SKIP_RPATH=ON
cmake --build arm-mqtt -- -j2

cmake -S gateway -B arm-full \
  -DCMAKE_TOOLCHAIN_FILE="$BASE/relocated-arm.cmake" \
  -DEDGELINK_ENABLE_MQTT=ON -DEDGELINK_ENABLE_SQLITE=ON \
  -DSQLite3_INCLUDE_DIR="$BASE/sqlite-armhf/usr/include" \
  -DSQLite3_LIBRARY="$BASE/sqlite-armhf/usr/lib/arm-linux-gnueabihf/libsqlite3.so" \
  -DCMAKE_SKIP_RPATH=ON
cmake --build arm-full -- -j2
```

工具链文件和依赖准备的完整重建方法见下节。构建输出为 `arm-full/edgelink`、`arm-mqtt/src/mqtt/edgelink_mqtt` 和 `arm-mqtt/src/storage/edgelink_db`，检查程序位于各自的 `src/mqtt` 和 `src/storage` 构建目录。

### 3.2 重新准备独立构建依赖

在 Ubuntu 普通用户终端执行。`WORK` 应是当前用户可写的新目录，源码目录应为 `$WORK/gateway`。下面的 `/opt/buster` 前置条件来自阶段一和阶段三环境。

先在 **Windows 项目根目录的 PowerShell** 打包并上传源码，然后登录 Ubuntu：

```powershell
$DeviceConfig = Join-Path $env:USERPROFILE '.ssh\ssh-devices\config'
$SourceArchive = Join-Path $env:TEMP ('edgelink-stage4-source-' + [guid]::NewGuid().ToString('N') + '.tar')
tar -cf $SourceArchive gateway
if ($LASTEXITCODE -ne 0) { throw '源码打包失败' }
scp -F $DeviceConfig $SourceArchive 'zm-tailscale:edgelink-stage4-source.tar'
if ($LASTEXITCODE -ne 0) { throw '源码上传失败' }
ssh -F $DeviceConfig zm-tailscale
```

进入 Ubuntu 后创建构建目录并解包，接着执行下面的依赖准备命令：

```bash
WORK="$HOME/edgelink-stage4-build"
mkdir -p "$WORK"
tar -xf "$HOME/edgelink-stage4-source.tar" -C "$WORK"
```

```bash
WORK="$HOME/edgelink-stage4-build"
mkdir -p "$WORK/host-compat" "$WORK/sqlite-armhf"
cd "$WORK"
test -f gateway/CMakeLists.txt
test -x /opt/buster/usr/bin/arm-linux-gnueabihf-g++-8

for package in libsqlite3-0 libsqlite3-dev; do
  deb="${package}_3.27.2-3+deb10u1_armhf.deb"
  curl -fL --retry 2 -o "$deb" "https://archive.debian.org/debian/pool/main/s/sqlite3/$deb"
  dpkg-deb -f "$deb" Package Version Architecture
  dpkg-deb -x "$deb" sqlite-armhf
done

for library in libisl.so.19 libopcodes-2.31.1-armhf.so libbfd-2.31.1-armhf.so; do
  ln -sf "/opt/buster/usr/lib/x86_64-linux-gnu/$library" "host-compat/$library"
  test -e "host-compat/$library" || exit 1
done

cat > relocated-arm.cmake <<'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_SYSROOT /opt/buster)
set(CMAKE_CXX_COMPILER /opt/buster/usr/bin/arm-linux-gnueabihf-g++-8)
set(CMAKE_AR /opt/buster/usr/bin/arm-linux-gnueabihf-ar)
set(CMAKE_RANLIB /opt/buster/usr/bin/arm-linux-gnueabihf-ranlib)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-rpath-link,/opt/buster/usr/lib/arm-linux-gnueabihf -Wl,-rpath-link,/opt/buster/lib/arm-linux-gnueabihf")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
```

随后在同一终端将上一节的 `BASE` 和 `cd` 目录都改为 `$WORK`，运行两组 CMake 命令。下载失败时先确认 Debian 软件包归档可访问，检查包版本为 `3.27.2-3+deb10u1` 且架构为 `armhf`。带 sudo 的标准 chroot 构建流程可参考 [阶段三复现文档](../阶段三/阶段三-MQTT消息通信复现.md) 和 [网关说明](../../gateway/README.md)，该标准流程的阶段四构建需要在 chroot 中准备 `libsqlite3-dev:armhf`。

编译结束后，在 Ubuntu 检查产物：

```bash
file arm-full/edgelink arm-mqtt/src/mqtt/edgelink_mqtt arm-mqtt/src/storage/edgelink_db
readelf -A arm-full/edgelink | grep Tag_ABI_VFP_args
readelf -d arm-full/edgelink | grep NEEDED
```

预期为 ARM EABI 程序，浮点参数使用 VFP 寄存器，完整网关依赖包含 `libsqlite3.so.0` 和 `libmosquitto.so.1`。

## 4. 上传开发板并运行检查

本次产物通过 Windows 的 SSH/SCP 经 Tailscale 转存到板端独立目录 `/home/debian/edgelink-stage4-resume.OaawkF`。上传以下六个文件到板端同一目录，确认来源是刚构建的产物：

```text
arm-full/edgelink
arm-mqtt/src/mqtt/edgelink_mqtt
arm-mqtt/src/mqtt/mqtt_state_check
arm-mqtt/src/mqtt/mqtt_client_check
arm-mqtt/src/storage/edgelink_db
arm-mqtt/src/storage/device_store_check
```

如果 Windows 已配置 `zm-tailscale` 和 `debian-board-tailscale`，可在 **Windows PowerShell** 中使用下面的命令转存。先把 `$VmWork` 改为实际构建目录。板端目录使用新建目录，命令结束时记下输出的路径。

```powershell
$DeviceConfig = Join-Path $env:USERPROFILE '.ssh\ssh-devices\config'
$VmWork = '/home/zm/edgelink-stage4-resume.FUvngC'
$StageDir = Join-Path $env:TEMP ('edgelink-stage4-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $StageDir | Out-Null
$BoardDir = (ssh -F $DeviceConfig -T debian-board-tailscale 'mktemp -d "$HOME/edgelink-stage4.XXXXXX"').Trim()
if ($LASTEXITCODE -ne 0 -or $BoardDir -notmatch '^/home/debian/edgelink-stage4\.[a-zA-Z0-9]+$') { throw '无法创建板端测试目录' }

$Files = @(
    'arm-full/edgelink',
    'arm-mqtt/src/mqtt/edgelink_mqtt',
    'arm-mqtt/src/mqtt/mqtt_state_check',
    'arm-mqtt/src/mqtt/mqtt_client_check',
    'arm-mqtt/src/storage/edgelink_db',
    'arm-mqtt/src/storage/device_store_check'
)
$Sources = @($Files | ForEach-Object { "zm-tailscale:${VmWork}/$_" })
scp -F $DeviceConfig @Sources "$StageDir/"
if ($LASTEXITCODE -ne 0) { throw '从虚拟机下载失败' }
$LocalFiles = @($Files | ForEach-Object { Join-Path $StageDir (Split-Path $_ -Leaf) })
scp -F $DeviceConfig @LocalFiles "debian-board-tailscale:${BoardDir}/"
if ($LASTEXITCODE -ne 0) { throw '上传开发板失败' }
Write-Output "板端目录：$BoardDir"
```

在**开发板终端**执行，目录根据实际上传位置调整：

```bash
cd ~/edgelink-stage4-resume.OaawkF
chmod +x edgelink edgelink_mqtt edgelink_db device_store_check mqtt_state_check mqtt_client_check
ldd ./edgelink ./edgelink_mqtt ./edgelink_db
./device_store_check
./mqtt_state_check
./mqtt_client_check
```

`ldd` 应找到所有运行库，三个检查程序均应通过。`device_store_check` 包括数据库重开、查询边界、事务回滚和锁释放后的恢复。

## 5. MQTT 到数据库的联调

### 5.1 开发板启动服务

连接开发板后，在**板端终端 A**执行。`18884` 是本次隔离测试端口，使用前检查它是否空闲。数据库文件必须放在当前用户可写的目录中。

```bash
cd ~/edgelink-stage4-resume.OaawkF
ss -ltn | grep ':18884 ' || true
printf 'listener 18884 127.0.0.1\nallow_anonymous true\npersistence false\n' > broker-acceptance.conf
/usr/sbin/mosquitto -c broker-acceptance.conf > broker-acceptance.log 2>&1 &
BROKER_PID=$!

./edgelink --mqtt-host 127.0.0.1 18884 --db ./acceptance.sqlite3
```

窗口显示 `[mqtt] ready` 后继续。该 Broker 使用本机回环地址和无认证测试配置，Windows 通过 SSH 转发访问。

### 5.2 Windows 启动模拟器

在 **Windows PowerShell 窗口 B** 建立转发并登录开发板。前提是本机 Tailscale 和 `debian-board-tailscale` SSH 别名已经配置并可以登录。

```powershell
ssh -F "$env:USERPROFILE\.ssh\ssh-devices\config" -o ClearAllForwardings=no -o ExitOnForwardFailure=yes -L 127.0.0.1:18884:127.0.0.1:18884 debian-board-tailscale
```

保持窗口 B 打开。另开两个 Windows PowerShell 窗口，在项目根目录分别运行：

```powershell
cd E:\Linux_project\demo1
python pc/sensor_sim/sensor_sim.py --host 127.0.0.1 --port 18884 --device-id sensor-01 --interval 2
```

```powershell
cd E:\Linux_project\demo1
python pc/sensor_sim/sensor_sim.py --host 127.0.0.1 --port 18884 --device-id sensor-02 --interval 3
```

如 Python 提示缺少 Paho，在 Windows 执行 `python -m pip install -r pc/sensor_sim/requirements.txt`。两个模拟器应显示连接成功并持续上报，板端终端 A 应显示两个设备的消息。

### 5.3 查询和重启验证

等待几条数据后，在两个模拟器窗口按 `Ctrl+C`。在已登录开发板的**窗口 B**执行：

```bash
cd ~/edgelink-stage4-resume.OaawkF
./edgelink_db ./acceptance.sqlite3 list
./edgelink_db ./acceptance.sqlite3 rename sensor-01 'Lab sensor'
./edgelink_db ./acceptance.sqlite3 show sensor-01

START=$(TZ=Asia/Shanghai date -d '1 hour ago' '+%F %T')
END=$(TZ=Asia/Shanghai date -d '1 minute' '+%F %T')
./edgelink_db ./acceptance.sqlite3 history sensor-01 "$START" "$END" 10
./edgelink_db ./acceptance.sqlite3 history sensor-02 "$START" "$END" 10
```

预期能看到两台设备、修改后的名称和各自的历史记录。在板端终端 A 按 `Ctrl+C` 停止网关，再使用同一数据库重新启动：

```bash
./edgelink --mqtt-host 127.0.0.1 18884 --db ./acceptance.sqlite3
```

回到窗口 B 再次执行 `show` 和两条 `history` 命令。设备名称与历史记录仍应存在，在线状态根据当前 MQTT 会话重新确认。结束后在终端 A 用 `Ctrl+C` 停止网关，执行 `kill "$BROKER_PID"` 停止测试 Broker，退出窗口 B 关闭转发。

## 6. 实测记录

| 检查 | 2026-11-07 结果 |
| --- | --- |
| 交叉构建与动态库 | GCC 8.3 构建独立网关、完整网关和查询工具成功。板端 `ldd` 找到所需库。 |
| 存储与 MQTT 检查 | 三个检查程序通过。 |
| 双设备数据 | 两个 Windows 模拟器经 Tailscale 上的 SSH 转发连接，各自的遥测写入板端数据库。 |
| 设备资料与历史 | 改名成功。固定采样时间测试验证了设备隔离和包含起点、排除终点的查询范围。 |
| 异常数据与重启 | 超范围湿度被拒绝。网关重启后名称和历史保留，后续上报继续写入。 |
| Broker 中断与恢复 | 状态转为 `unknown`，Broker 恢复后网关重新订阅并继续写入。 |
| 数据库完整性 | `PRAGMA integrity_check` 返回 `ok`，外键检查通过，操作记录包含注册、改名和状态变化。 |

本次板端测试目录为 `/home/debian/edgelink-stage4-resume.OaawkF`，包含 `stage4-test.sqlite3`、`checks.log`、`gateway-first.log`、`gateway-full.log`、`broker-test.log` 和 `broker-restart.log`。构建日志位于 Ubuntu 的 `/home/zm/edgelink-stage4-resume.FUvngC`。

## 7. 排查

- **提示 `mosquitto: command not found`**：本板可执行文件位于 `/usr/sbin/mosquitto`，非交互 SSH 的 `PATH` 可能未包含 `/usr/sbin`。
- **找不到 SQLite 头文件或库**：检查编译配置指定的头文件和 ARM 库路径，板上的运行库不能代替编译端开发文件。
- **网关报数据库打开或写入失败**：确认数据库目录可写、磁盘空间充足，并查看网关的 `[storage]` 日志。一次遥测写入失败会回滚并记录错误，后续上报继续处理。
- **模拟器连接失败**：确认板上测试 Broker 和窗口 B 的 SSH 转发仍在运行，三个位置的端口均为 `18884`。
- **查询结果为空**：检查设备 ID、设备采样时间和查询范围。结束时间不包含在结果内，时间使用北京时间。
