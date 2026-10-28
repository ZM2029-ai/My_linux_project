<<<<<<< HEAD
# EdgeLink —— 基于 i.MX6ULL 的轻量级物联网平台

边缘网关 + 网页管理端的轻量级物联网系统。开发板运行网关核心（C++），Windows 侧运行设备模拟与测试工具（Python），浏览器运行管理页面。

## 三端划分

| 目录 | 运行在哪 | 技术栈 | 说明 |
| --- | --- | --- | --- |
| [`gateway/`](gateway/README.md) | i.MX6ULL 开发板 | C++14 交叉编译 | 网关核心：TCP/MQTT 通信、SQLite 存储、HTTP API |
| [`pc/`](pc/README.md) | Windows | Python 3 | 设备信号模拟、MP3 over TCP 发送等 PC 侧工具 |
| [`web/`](web/README.md) | 浏览器 | HTML/CSS/JS | 网页管理端（数据由网关 HTTP API 提供） |
| [`protocol/`](protocol/README.md) | —— | —— | 三端共用的「契约」：MQTT 主题、JSON 字段、TCP 帧格式 |
| [`scripts/`](scripts) | 虚拟机 / Windows | Bash | 三端通用：交叉构建、部署 |

## 文档

| 文档 | 内容 |
| --- | --- |
| [`README/`](README) | 开发计划、AI 开发约束、开发板环境记录 |
| [`复现文档/阶段一/`](复现文档/阶段一) | 交叉编译环境搭建与验证（可完整复现） |

## 当前进度

| 阶段 | 状态 |
| --- | --- |
| 阶段一：环境准备与工程骨架 | 进行中（环境已验证，骨架已建立，日志/配置待接） |
| 阶段二：TCP 音乐流式播放 | 未开始 |
| 阶段三：MQTT 通信 | 未开始 |
| 阶段四：设备管理与数据存储 | 未开始 |
| 阶段五：HTTP API 与网页管理端 | 未开始 |
| 阶段六：远程控制、告警与公网访问 | 未开始 |
| 阶段七：部署与稳定性测试 | 未开始 |

## 关键约束（务必遵守）

1. **交叉工具链必须是 GCC 8.3**：开发板是 Debian 10 / glibc 2.28 / `GLIBCXX_3.4.25`，Ubuntu 24.04 自带的 GCC 13 编出来的程序**无法运行**（缺 `GLIBC_2.34`）。详见复现文档。
2. **C++ 基线为 C++14**，可以用 C++17 但**避开 `std::filesystem`**（需要 GCC 9）。
3. **所有脚本使用 LF 换行**（已由 `.gitattributes` 保证）。

## 快速开始

```bash
# 1) 在虚拟机上按复现文档搭好 chroot 环境（一次性）
#    详见 复现文档/阶段一/阶段一-交叉编译环境搭建与验证.md

# 2) 配置源码路径后构建（在虚拟机内执行）
REPO_DIR=/mnt/hgfs/demo1 ./scripts/build.sh

# 3) 部署到开发板并试运行
./scripts/deploy.sh
```

## 开发约定

- **一个功能一个文件夹**：`gateway/src/<功能>/`、`pc/<功能>/`，功能文件夹在实现该功能时创建，不预先建空目录。
- 三端共享的协议定义统一放 `protocol/`，**不要**在各端重复定义一份。
- 凭据（开发板密码、token）不写入仓库，用环境变量或本地文件（已在 `.gitignore` 中排除）。
=======
# My_linux_project
A Linux study record of a 29th-grade college student
>>>>>>> 887e733e8228579bc729a4dded3169846d97e3f4
