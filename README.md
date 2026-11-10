<<<<<<< HEAD

# EdgeLink

# — 基于 i.MX6ULL 的轻量级物联网平台

边缘网关 + 网页管理端的轻量级物联网系统。开发板运行网关核心（C++），Windows 侧运行设备模拟与测试工具（Python），浏览器运行管理页面。

## 三端划分

| 目录                                | 运行在哪          | 技术栈         | 说明                                  |
| --------------------------------- | ------------- | ----------- | ----------------------------------- |
| [`gateway/`](gateway/README.md)   | i.MX6ULL 开发板  | C++14 交叉编译  | 网关核心：TCP/MQTT 通信、SQLite 存储、HTTP API |
| [`pc/`](pc/README.md)             | Windows       | Python 3    | 设备信号模拟、MP3 over TCP 发送等 PC 侧工具      |
| [`web/`](web/README.md)           | 浏览器           | HTML/CSS/JS | 网页管理端（数据由网关 HTTP API 提供）            |
| [`protocol/`](protocol/README.md) | ——            | ——          | 三端共用的「契约」：MQTT 主题、JSON 字段、TCP 帧格式   |
| [`scripts/`](scripts)             | 虚拟机 / Windows | Bash        | 三端通用：交叉构建、部署                        |

## 工程骨架

```
demo1/
├── README.md                    工程速览
├── 操作手册.md                    启动命令汇总
├── .gitattributes / .gitignore  强制 LF；排除凭据与产物
│
├── protocol/                    ★ 三端契约，使用协议的汇总（纯文档）
│   ├── mqtt-topics.md           4 个主题、QoS、retain、online/offline/unknown 规则
│   ├── device-telemetry.md      遥测/状态 JSON 与校验边界（-100~200、0~100、深度≤8）
│   ├── control-command.md       set_reporting 命令、command_id 去重、256 条回执缓存
│   ├── http-api.md              REST 路径、状态码、账号文件 username:role:password
│   └── music-stream.md          ELMS v2：8 字节头（magic/ver/type/len）+ ≤4096 载荷
│
├── gateway/                     ★ C++ 网关（唯一需要交叉编译的部分）
│   ├── CMakeLists.txt           顶层：选项开关 + 4 个 add_subdirectory 分支
│   ├── cmake/toolchain-arm-linux-gnueabihf.cmake   钉死 gcc-8/g++-8
│   ├── config/mosquitto-stage3.conf                无认证局域网 Broker 配置
│   ├── vendor/cpp-httplib/httplib.h                单头 HTTP 库（第三方，8.7k 行）
│   ├── src/
│   │   ├── main.cpp             ★总入口：uname/glibc 自检 + 三种模式分发
│   │   ├── device_fields.hpp    跨模块公共：DevicePresence、validDeviceId、validTimestamp
│   │   ├── net/                 music_receiver —— TCP 监听、拆帧、背压
│   │   ├── audio/               music_player —— mpg123 解码 + ALSA 输出（Pimpl）
│   │   ├── mqtt/                mqtt_client（连接状态机）+ mqtt_gateway（组装）+ 
│   │   │                        device_message（校验）+ device_registry（256 设备状态）
│   │   │                        mqtt_main.cpp（独立可执行）+ README（阅读顺序）
│   │   ├── storage/             device_store（3 张表 DAO）+ storage_main（edgelink_db CLI）
│   │   └── http/                http_main（路由）+ auth_config（账号文件）
│   │                            + control_service（MQTT 命令/回执，后台线程）
│   └── tests/                   6 个独立检查程序（无 Broker/无板卡也能跑一部分）
│
├── pc/                          Windows 侧，Python 3，各文件夹自成一体的可执行脚本
│   ├── sensor_sim/              sensor_sim.py（遥测+心跳+遗嘱+命令去重）+ unittest
│   └── music_sender/            sender.py（分片发送 + 校验 RESULT）
│
├── web/                         原生前端，由 edgelink_http 以静态目录挂载
│   ├── index.html               单页：列表视图 / 详情视图（?device=id 切换）
│   └── assets/app.js            每 10s 轮询、SVG 手绘曲线、控制按钮轮询回执 7s
│
├── scripts/                     build.sh（bind mount → chroot → 取回 + readelf 自检）
│                                deploy.sh（scp 到 .new 再 mv，可 RUN_AFTER_DEPLOY）
├── 复现文档/                     阶段一~六复现 + 阶段四 tun 内核模块案例
├── build/stage6-20261108/       一次板端联调留下的 arm-bundle.tar、部署脚本、日志、pid
└── README/                      计划书（365 行）、板卡配置、给 AI Agent 的 11 条约束
```

## 开发要求

1. **交叉工具链必须是 GCC 8.3**：开发板是 Debian 10 / glibc 2.28 / `GLIBCXX_3.4.25`，Ubuntu 24.04 自带的 GCC 13 编出来的程序**无法运行**（缺 `GLIBC_2.34`）。详见复现文档。
2. **C++ 基线为 C++14**，可以用 C++17 但**避开 `std::filesystem`**（需要 GCC 9）。
3. **所有脚本使用 LF 换行**（已由 `.gitattributes` 保证）。

## 项目的工程思维

- **一个功能一个文件夹**：`gateway/src/<功能>/`、`pc/<功能>/`，功能文件夹在实现该功能时创建，不预先建空目录。

- 三端共享的协议定义统一放 `protocol/`，**不要**在各端重复定义一份。

- 交叉编译

- git管理，仓库地址https://github.com/ZM2029-ai/My_linux_project.git

## 技术栈

| 层              | 技术                                                                                                                                                                                                                            |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **目标运行环境**     | i.MX6ULL · ARMv7 armhf 硬浮点 · Debian 10 (buster) · glibc 2.28 · GLIBCXX_3.4.25 · 内核 4.19.35-imx6 · 单核 792 MHz / 486 MiB                                                                                                        |
| **主要语言**       | C++14 基线（允许 C++17，**禁用 `std::filesystem`**）                                                                                                                                                                                   |
| **交叉工具链**      | `arm-linux-gnueabihf-gcc-8 / g++-8`（GCC 8.3.0-6），**钉死绝对路径**防误用宿主 GCC 13                                                                                                                                                       |
| **构建系统**       | CMake 3.13（chroot 内就是这个版本，`cmake_minimum_required` 不能抬高）+ 工具链文件 + `-D` 选项裁剪                                                                                                                                                   |
| **构建环境**       | `debootstrap` 建 Debian 10 minimal chroot（amd64 编译机）+ `dpkg --add-architecture armhf` 多架构 + `mount --bind`                                                                                                                     |
| **C++ 第三方库**   | `libmosquitto` 1.5.7（MQTT 客户端）<br>`nlohmann/json` 3.5.0（**仅头文件**，不给板端加动态依赖）<br>`cpp-httplib` **v0.18.7**（vendor 进仓库的单头文件库）<br>SQLite 3.27.2（**直接用 C API**，无 ORM）<br>`mpg123` 1.25.10（MP3 解码）+ ALSA `libasound2` 1.1.8（PCM 输出） |
| **C++ 标准库用法**  | `std::thread` / `mutex` / `condition_variable` / `atomic` / `chrono::steady_clock` / `functional` 回调 / `map` / `unique_ptr` / Pimpl 惯用法 / `std::sig_atomic_t`                                                                 |
| **Linux 系统编程** | POSIX Socket（`socket/bind/listen/accept`）、`recv/send` + `MSG_NOSIGNAL` + `EINTR` 重试、`uname()`、`gnu_get_libc_version()`、`signal(SIGINT/SIGTERM)`、文件 IO、`perror`                                                                |
| **并发模型**       | 网关：**单事件循环线程 + 回调全在其中执行**（无跨线程锁）；控制服务：**独立 MQTT 后台线程 + mutex/CV/atomic**；HTTP：httplib `ThreadPool(4)`                                                                                                                         |
| **数据存储**       | SQLite 三表（`devices` / `telemetry` / `operation_log`）+ 索引 + 单事务 + `PRAGMA user_version` 版本号 + `ON CONFLICT`（故要求 ≥3.24）                                                                                                         |
| **应用层协议**      | MQTT **3.1.1**（QoS 0/1、retain、LWT 遗嘱、clean session）<br>HTTP **REST + JSON**（HTTP/1.1）<br>自定义二进制帧 **"ELMS v2"**（8 字节头：magic/ver/type/len，**大端**，载荷 ≤4096）                                                                      |
| **前端**         | 原生 **HTML5 + CSS3 + Vanilla JS（ES6+）**：`fetch` / `async-await` / `URLSearchParams` / **手写 SVG DOM 折线图（`createElementNS`）** / `setInterval` 10s 轮询；**无框架、无 npm、无打包**                                                           |
| **PC 侧**       | **Python 3**（argparse / threading / datetime+timezone / OrderedDict / regex）<br>**paho-mqtt ≥2.0,<3.0**（`CallbackAPIVersion.VERSION2`）<br>`socket` + `struct.Struct("!4sBBH")` + `pathlib` + `unittest`                       |
| **脚本 / 运维**    | **Bash**（`set -euo pipefail`）+ `mount --bind` + `chroot` + `readelf -d/-V` 符号版本自检 + `file`；部署用 **scp 传 `.new` 再 `mv`** 的原子替换；SSH 本地端口转发 `ssh -L`；`tar` 打包传源码                                                                  |
| **网络互联**       | **Tailscale**（`100.x` CGNAT 地址 + `~/.ssh/ssh-devices/config` 别名 `zm-tailscale` / `debian-board-tailscale`），公网验收计划走 HTTPS 隧道                                                                                                   |
| **内核（专题案例）**   | Linux **kbuild 外部模块**构建：`make -C O= ARCH=arm CROSS_COMPILE=`、`olddefconfig`、`kernelrelease`、`CONFIG_MODVERSIONS` / `Module.symvers`、`insmod`、`dmesg`、TUN/TAP 驱动                                                               |
| **版本管理**       | Git + `.gitattributes` 强制 **LF**（脚本在 Linux 上必须）+ `.gitignore` 排除凭据与产物                                                                                                                                                         |
