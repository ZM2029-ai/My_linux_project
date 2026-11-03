# 阶段二：TCP 音乐流式播放复现

本文记录 Windows 发送 MP3、i.MX6ULL 通过 TCP 接收并播放的环境和步骤。阶段二按用户确认结题；音频播放与 TCP 播放已由用户实测。最终源码的标准 CMake 构建及下文全部命令尚未从零完整复核，使用时应逐项执行验收，不将预期输出当作实测结果。

## 1. 工作方式

```text
Windows Python 发送本地 MP3
  → TCP 协议 v2 → 网关按块接收
  → libmpg123 解码 PCM → ALSA → WM8960 耳机接口
```

网关同步接收、解码和播放，应用层不保存完整 MP3；播放较慢时通过 TCP 背压限制发送。一次连接处理一首歌，正常结束时网关等待 PCM 播放完再返回成功。协议细节见 `protocol/music-stream.md`，两端需使用相同版本。

## 2. 环境和前置条件

| 项目 | 本次环境 |
| --- | --- |
| 开发板 | i.MX6ULL，ARMv7 armhf，Debian 10，glibc 2.28 |
| 音频 | WM8960，ALSA 声卡 0；耳机连接板载接口 |
| 编译机 | Ubuntu 虚拟机，Debian 10 chroot `/opt/buster`，GCC 8.3 交叉工具链 |
| 发送端 | Windows、Python 3；仅使用标准库 |
| 依赖 | 板上 `libasound2` 1.1.8-1、`libmpg123-0` 1.25.10-2 |

先按 `复现文档/阶段一/阶段一-交叉编译环境搭建与验证.md` 完成 chroot 和工具链搭建。Ubuntu、Windows 和开发板不必使用同一 Wi-Fi，但 Windows 必须能访问网关 TCP 端口，Ubuntu 必须能 SSH/SCP 访问开发板。以下 IP 为示例，请替换为当前实际地址；密码不写入仓库。

## 3. 准备依赖

### 3.1 Ubuntu 虚拟机：交叉编译依赖

在 Ubuntu 宿主终端执行（不是开发板终端）：

```bash
sudo chroot /opt/buster dpkg --add-architecture armhf
sudo chroot /opt/buster apt-get update
sudo chroot /opt/buster apt-get install libmpg123-dev:armhf libasound2-dev:armhf
sudo chroot /opt/buster dpkg -l libmpg123-dev:armhf libasound2-dev:armhf
```

两个包状态应为 `ii`。本次安装候选版本分别为 `1.25.10-2` 和 `1.1.8-1`。chroot 本身是 amd64，必须安装 `:armhf` 开发包；若更新失败，先检查阶段一文档中的归档源与 DNS 配置。

### 3.2 开发板：运行库与声卡

登录开发板，在板上执行：

```bash
sudo apt-get update
sudo apt-get install libmpg123-0 libasound2
cat /proc/asound/cards
aplay -l
```

本次实测声卡为 `wm8960audio`，播放设备包括 card 0、device 0。安装候选包并不等于已验证音频输出，仍需连接耳机实际确认。

可以查看和调整耳机音量（先调低再逐步提高）：

```bash
amixer -c 0 sget Headphone
amixer -c 0 sset Headphone 50%
```

## 4. 在 Ubuntu 交叉编译

确保 Ubuntu 能访问**当前完整源码**。本次使用 VMware 共享路径 `/mnt/hgfs/share_for_vmware/demo1`；若共享目录失效，先在 VMware 中恢复挂载。若这是另一份源码副本，先确认其包含 `gateway/src/audio/music_player.cpp` 和协议 v2 修改。

```bash
cd /mnt/hgfs/share_for_vmware/demo1
REPO_DIR=/mnt/hgfs/share_for_vmware/demo1 ./scripts/build.sh
```

脚本默认将产物放在 `~/edgelink-bin/edgelink`。脚本会复用已经存在的 bind mount；若之前挂载过不同源码或构建目录，先用 `findmnt /opt/buster/work` 和 `findmnt /opt/buster/build` 确认实际来源，避免编译旧副本。脚本输出 `file`、`readelf` 的信息供检查；这些检查命令失败时脚本仍可能继续，应人工确认架构和符号版本。

```bash
file ~/edgelink-bin/edgelink
readelf -d ~/edgelink-bin/edgelink | grep NEEDED
readelf -V ~/edgelink-bin/edgelink | grep -E 'Name: (GLIBC|GLIBCXX|CXXABI)' | sort -u
```

确认程序为 ARM 32 位、目标动态库可用，所需 glibc 不超过 2.28、GLIBCXX 不超过 3.4.25。若 CMake 找不到 ARM 开发文件，检查步骤 3.1 的包和 `/usr/include/mpg123.h`、`/usr/lib/arm-linux-gnueabihf/libmpg123.so`、`libasound.so`。

## 5. 部署与运行

在 Ubuntu 的项目目录执行，将地址换成开发板当前 IP：

```bash
BOARD_HOST=10.192.110.142 RUN_AFTER_DEPLOY=0 ./scripts/deploy.sh
ssh debian@10.192.110.142
```

脚本默认部署到 `/home/debian/edgelink/edgelink`。在开发板终端检查依赖并启动：

```bash
cd /home/debian/edgelink
ldd ./edgelink
./edgelink --music-port 9000
```

`ldd` 不应有 `not found`；启动后应显示 `[music] listening on port 9000`。保持该终端打开；按 `Ctrl+C` 停止网关。默认不带参数启动仅执行环境自检。

## 6. Windows 发送和验收

在 Windows PowerShell 中执行；替换 IP、端口和 MP3 路径：

```powershell
cd E:\Linux_project\demo1
python pc/music_sender/sender.py 10.192.110.142 9000 "E:\1月10日.MP3"
```

听到声音后确认内容连续完整；正常结束后，开发板应打印接收字节数和 `[music] playback complete`，Windows 应打印 `网关播放完成：… 字节`。文件大小可用 `(Get-Item "E:\1月10日.MP3").Length` 查看；发送的字节数应与文件大小一致。此比较不等于逐字节完整性校验。

可测试拆分发送，增加对 TCP 分段的覆盖：

```powershell
python pc/music_sender/sender.py 10.192.110.142 9000 "E:\1月10日.MP3" --fragment-size 3
```

TCP 可以合并多次发送，因此该选项不能保证接收端每次都收到分段。发送阶段单次操作默认超时 30 秒，可用 `--timeout` 调整；END 后等待结果不设超时。

## 7. 实测记录与复核清单

已记录的测试：协议 v1 的普通和拆分发送均收到 51,459 字节，断线后可再次连接；本地音频测试可连续播放；正式音频模块的本地播放由用户确认成功；协议 v2 的非法消息返回协议错误，用户确认 TCP 播放链路跑通。长 MP3 中断 Windows 发送后音乐未立即停止。

以下清单用于重新复现时逐项确认，尚无具体测量结果的项目不预先打勾：

- [ ] 标准 CMake 构建与 `scripts/build.sh` / `scripts/deploy.sh` 从当前源码完整通过。
- [ ] 确认长 MP3 在整首发送完成前开始播放，声音完整且无持续卡顿。
- [ ] 播放结束后不重启网关，再播放另一首。
- [ ] 发送中断后观察停止延迟，并确认资源释放和后续连接。
- [ ] 声卡占用、无效或空音频、错误协议等情况下检查错误提示及恢复。
- [ ] 在实际负载下记录 CPU、内存及缓冲表现。

## 8. 常见问题和已知限制

- `ALSA open: Device or resource busy`：其他程序可能占用播放设备。退出板载播放器；可用 `sudo sh -c 'for f in /proc/[0-9]*/fd/*; do t=$(readlink "$f" 2>/dev/null); case "$t" in /dev/snd/*) echo "$f -> $t";; esac; done'` 查询，再用 `ps -fp PID` 识别进程。不要随意停止未知服务。
- 终止 Windows 发送端后音乐可能不会立即停止：当前同步接收与播放，TCP/音频缓冲中的数据可能继续被处理；暂停、切歌与低延迟停止尚未实现。
- 发送端在等待 RESULT 时没有超时；如网关或网络无响应，需手动中断排查。
- 网关一次仅处理一个客户端；发送过程中音频失败，客户端可能先看到发送失败或连接关闭，未必收到 RESULT。
- 网关与发送端须同时使用协议 v2；旧 v1 网关的“接收成功”不代表播放完成。
- 测试用 `edgelink_audio_test` 是曾在临时目录直接编译的产物；正式复现应以当前源码构建的 `edgelink` 为准。
