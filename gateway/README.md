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

构建脚本做的事：bind mount 源码/构建目录 → chroot 内 `cmake` + `cmake --build` → 取回产物并显示符号版本自检。

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
