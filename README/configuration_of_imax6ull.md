处理器：Freescale i.MX6 UltraLite

cpu架构：armv7l,32位

操作系统： Debian GNU/Linux 10（Buster）。

C库：glibc 2.28

ssh：active

ABI：VFP 浮点参数传递



硬盘情况

- 根分区 `/`：7.1 GB

- 已使用：758 MB

- 剩余：6.0 GB

- `/boot`：剩余 22 MB

内存情况

- 内存： 486 MiB，约 512 MB。

- 当前可用内存： 358 MiB。

- Swap： 未启用。

- 存储： 根分区剩余 6.0 GB。

---

## 详细环境实测记录

记录时间：2026-10-28。以下均为在开发板上直接执行命令得到的实测结果。

### 系统与内核

- 主机名：`npi`
- 内核：`4.19.35-imx6`，`#1.2307stable SMP PREEMPT`，构建时间 2023-07-27
- 系统：Debian GNU/Linux 10 (buster)，`VERSION_ID="10"`
- 包架构：`armhf`（硬浮点，`arm-linux-gnueabihf`），与已记录的 VFP 参数传递一致
- init 系统：systemd（`/bin/systemctl` 存在）

### C++ 运行库（二进制兼容性关键项）

- libstdc++ 路径：`/usr/lib/arm-linux-gnueabihf/libstdc++.so.6`
- libstdc++ 支持的最高版本符号：`GLIBCXX_3.4.25`（对应 GCC 8.3）
- 已确认：glibc 2.28

### CPU

- 型号：ARMv7 Processor rev 5 (v7l)
- 核心数：1（nproc = 1，单核）
- CPU 特性：`half thumb fastmult vfp edsp neon vfpv3 tls vfpv4 idiva idivt vfpd32 lpae`
- 可用频率：198000 / 396000 / 528000 / 792000（即 198 / 396 / 528 / **792 MHz**）
- 调频策略：`ondemand`
- 结论：具备 NEON / VFPv4，浮点与 NEON 加速可用；单核意味着网络收包、MP3 解码、业务逻辑共享一个核，资源预算需按单核 792 MHz 计算。

### 板上开发工具

| 工具 | 状态 |
| --- | --- |
| gcc | 存在（`/usr/bin/gcc`），版本 **8.3.0**（Debian 8.3.0-6，仅 C 编译器） |
| g++ | 不存在 |
| make | 不存在 |
| cmake | 不存在 |
| git | 不存在 |
| python3 | 不存在 |
| sqlite3（命令行） | 不存在 |
| mosquitto | 不存在 |

结论：板上不具备 C++ 构建能力，必须采用「Windows 编写 → Ubuntu 交叉编译 → 部署」流程。

### 已预装的运行库

| 库 | 版本 |
| --- | --- |
| libsqlite3-0 | 3.27.2-3+deb10u1 |
| libssl1.1 | 1.1.1n-0+deb10u3 |
| libcurl4 | 7.64.0-4+deb10u2 |
| zlib1g | 1.2.11.dfsg-1+deb10u1 |
| mosquitto / libmosquitto | 未安装 |

备注：SQLite 运行库已在板上，阶段四可直接链接；MQTT Broker 与客户端库需另行部署（apt 安装或交叉编译，取决于软件源可用性）。

### 可安装的软件包（apt 候选版本）

| 软件包 | 候选版本 | 用途 |
| --- | --- | --- |
| g++ | 4:8.3.0-1（GCC 8.3） | 板上本地编译 C++（应急/调试手段） |
| make | 4.2.1-1.2 | 板上构建 |
| cmake | 3.13.4-1 | 板上构建 |
| git | 1:2.20.1-2+deb10u3 | 板上版本管理 |
| python3 | 3.7.3-1 | 板上脚本/工具 |
| sqlite3 | 3.27.2-3+deb10u1 | 数据库命令行工具 |
| libsqlite3-dev | 3.27.2-3+deb10u1 | 阶段四开发头文件与库 |
| mosquitto | 1.5.7-1+deb10u1 | 阶段三 MQTT Broker |
| libmosquitto-dev | 1.5.7-1+deb10u1 | 阶段三 MQTT 客户端开发库（C API，可在 C++ 中调用） |

重要结论：

1. 板上软件源**可用且版本齐全**，阶段三、四需要的 Mosquitto、SQLite 开发库都可以直接在板上 `apt install`，不必交叉编译这些基础组件。
2. 板上 gcc 为 8.3.0，与 libstdc++6 8.3.0-6、`GLIBCXX_3.4.25` 完全同代，说明板厂系统就按 GCC 8.3 构建，交叉工具链选 GCC 8.3 是与之严格对齐的选择。
3. 板上装 g++ 后具备本地 C++ 编译能力，可作为「交叉编译产物无法运行」时的 A/B 对照手段，但不作为正式流程（正式流程仍按计划交叉编译）。

### 网络

- wlan0：`10.192.110.142/24`，网关 `10.192.110.93`，DNS `10.192.110.93`
- usb0：`192.168.7.2/30`（USB 网卡，可作为 WiFi 不可用时的备用通道）
- 外网连通性：`PING-OK`（可访问 223.5.5.5）
- SSH：22 端口处于监听状态（IPv4 + IPv6）；`sshd_config` 中 `Port` / `PermitRootLogin` / `PasswordAuthentication` / `PubkeyAuthentication` 均已注释（grep 无输出），即使用默认值：端口 22、允许密码认证
- 待补：从 Windows 侧实际验证 `ssh` / `scp` 连接（凭据不写入本项目文件，避免进 Git 版本库）

### 账户与权限

- 当前用户：`debian`，uid/gid 1000，属于 `admin`、`sudo`、`audio`、`gpio`、`i2c`、`spi`、`netdev`、`systemd-journal` 等组
- sudo：需要密码（`SUDO-NEEDS-PW`）
- `/home`：仅 root 可写（`drwxr-xr-x`），`/home` 下共 3 项
- `/opt`：root 拥有，内容为 `backup`、`scripts`、`source`（板厂预置，暂无本项目所需运行库；`source` 可能含板厂示例/源码，需要时再查）
- `/usr/local`：root 拥有，含 `bin`、`etc`、`include`、`lib`、`man`、`qt-app`、`sbin`、`share`、`src` 等（板厂 Qt 与 SDK 环境）；`/usr/local/lib` 下主要为 `python2.7`，未发现本项目要用的第三方 C/C++ 开发库
- 权限结论：正式部署目录放在 `/home/debian` 下（可写、免 sudo）；涉及 systemd 服务、apt 安装等才需要 sudo（密码方式）

### 存储

- `/dev/mmcblk1p2` 挂载 `/`：7.1G，已用 758M，可用 6.0G
- `/dev/mmcblk1p1` 挂载 `/boot`：40M，已用 18M，可用仅 22M
- 结论：程序、配置、日志、数据库均应放在 `/` 下，且不要改动 `/boot`。

### 软件源（apt）

```text
deb http://mirrors.aliyun.com/debian-archive/debian buster main contrib non-free
deb [arch=armhf] https://cloud.embedfire.com/mirrors/ebf-debian buster main
deb [arch=armhf] https://cloud.embedfire.com/mirrors/ebf-debian carp-imx6 main
```

备注：buster 已 EOL，官方主源已迁至 `debian-archive`（当前配置使用的正是 archive 源）。软件源中是否真的能安装 g++ / mosquitto 等包，尚未验证。

### 时间与时区

- 系统时区：`Asia/Shanghai`（CST，+0800）
- `timedatectl`：Local time = CST、Universal time 与 RTC time 一致
- NTP：服务 active，`System clock synchronized: yes`，时间已同步
- 备注：`/etc/timezone` 文件内容为 `Etc/UTC`，但实际生效时区由 `/etc/localtime` 决定为 Asia/Shanghai，以 `timedatectl` 为准
- 结论：时间无需处理；阶段四时间戳与日志统一按 Asia/Shanghai（CST +0800）口径

### 内核模块

- `/lib/modules/4.19.35-imx6` 存在，与运行内核版本一致，具备加载内核模块的条件（阶段二音频相关模块在此核对）

---

## 二进制兼容性结论

目标板运行环境上限：

- glibc：2.28
- libstdc++：`GLIBCXX_3.4.25`（约等于 GCC 8.3）
- 架构：armhf（硬浮点，`arm-linux-gnueabihf`）

由此确定交叉编译工具链的选择原则：

1. 工具链不得高于 GCC 8.3。GCC 9.3 对应的 `GLIBCXX_3.4.28`、GCC 11/12 对应的更高版本，都会导致程序在板上启动时报 `GLIBCXX_3.4.28 not found` 一类的版本错误。
2. 不能用 Ubuntu 20.04 / 22.04 自带的默认交叉工具链（GCC 9.x / 11.x，版本过高）。
3. 候选方案（按优先级）：
   - 首选：与目标板同代同版本的 GCC 8.3 交叉工具链。Debian 10 buster 自带的 `gcc-8-arm-linux-gnueabihf` 即为 GCC 8.3，与板上 `libstdc++6 8.3.0-6` 严格对齐，兼容性最有保证；
   - 次选：Ubuntu 18.04 的 `gcc-arm-linux-gnueabihf`（GCC 7.5，`GLIBCXX_3.4.24`，安全但更旧）；
   - 备选：开发板厂商（embedfire）提供的 SDK 交叉工具链，需先确认其 GCC 版本是否为 8.3。
4. 板上实测佐证：板上 `gcc 8.3.0-6` + `libstdc++6 8.3.0-6` + `GLIBCXX_3.4.25` 三者一致，说明板厂系统正是用 GCC 8.3 构建，交叉工具链选 GCC 8.3 属于精确对齐，而非猜测。
5. 第三方库（Mosquitto 客户端、MP3 解码库等）同样必须用这套不高于 GCC 8 的工具链自行交叉编译，不能直接使用高版本 GCC 预编译的二进制包。例外是板上软件源中已提供 ARM 版本的软件包（见「可安装的软件包」），这类直接用 apt 安装即可。
6. 部署与验证方式：部署到 `/home/debian` 下可写目录，用 `ldd` / `readelf` 检查依赖，并在板上实际运行验证。

## 分阶段组件部署结论（基于本次实测）

- 阶段三 MQTT：Broker 用板上 `apt install mosquitto`（1.5.7）即可；客户端库用 `libmosquitto-dev`（1.5.7，C API，C++ 中可直接调用），无需为 MQTT 引入高版本工具链依赖。
- 阶段四 SQLite：板上已装 `libsqlite3-0` 3.27.2，开发用 `apt install libsqlite3-dev`；交叉编译时使用同版本头文件与库。
- 阶段二音频/ALSA：已识别 WM8960 和 ALSA 运行库，详见下方阶段二记录；解码库及交叉编译依赖仍需验证。
- 阶段一最小 C++ 程序：用 GCC 8.3 交叉工具链编译，`scp` 到 `/home/debian` 下运行；若出现运行库版本问题，可临时在板上装 g++ 8.3 本地编译作为对照。

---

## 待补充项

### 板子上待查（一次补齐，避免重复登录）

板子侧信息已基本查完（2026-10-28）。剩余仅两项，可在进入对应阶段时一并处理：

- 阶段一：从 Windows 实测 `ssh` / `scp` 连接开发板，验证部署流程（凭据不写入本文件）
- 阶段二/三（需要时）：`/opt/source` 下板厂示例内容、音频相关软件包与模块

### 板子之外待确认

- Ubuntu 虚拟机的版本
- 交叉编译工具链是否已安装、版本是多少（目标：GCC 8.3，`arm-linux-gnueabihf`）

### 按计划推迟

- 音频流式解码、交叉编译依赖及播放参数留到阶段二继续验证
- Mosquitto 部署方式已定为板上 `apt install`（见「分阶段组件部署结论」），仅客户端库的具体调用细节留到阶段三
- 公网隧道相关配置在阶段六确定

## 阶段二音频环境检查

2026-10-30 通过 SSH 只读查询：

- `/proc/asound/cards` 显示声卡 0：`wm8960audio`（WM8960）。`aplay -l` 显示播放设备 0（`wm8960-hifi-0`）和设备 1（`HiFi-ASRC-FE`）。
- `aplay -L` 列出 `default:CARD=wm8960audio`、`hw:CARD=wm8960audio,DEV=0`、`plughw:CARD=wm8960audio,DEV=0` 等设备；最终使用的设备和参数需实测。
- 已安装 `libasound2:armhf` 1.1.8-1 和 `aplay`；`libasound2-dev` 未安装，apt 候选版本 1.1.8-1。
- `libmpg123-0` / `libmpg123-dev` 未安装，apt 候选版本均为 1.25.10-2；`libmad0` / `libmad0-dev` 未安装，候选版本均为 0.15.1b-10。候选版本不等于已验证可以安装或适合本项目。
- 用户已确认板上音频设备可用；本次查询未进行实际播放测试，也没有安装软件或修改板上配置。
- 待调查：解码库的流式接口、ARM 开发头文件及链接库在 Debian 10 chroot 中的获取方式、PCM 格式和声道/采样率支持，以及实际播放时的 CPU 和缓冲表现。


