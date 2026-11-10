# TCP 音乐发送端

使用 Python 3 标准库，无需额外安装依赖。发送端使用 [音乐协议 v2](../../protocol/music-stream.md)，接收端也必须是 v2。以下示例适用于已从当前 v2 源码构建并部署的 `edgelink`：

```sh
./edgelink --music-port 9000
```

开发板现存的 `/home/debian/edgelink/edgelink` 是旧 v1 程序；使用此前构建的 `edgelink_audio_test` 的启动步骤见[操作手册](../../操作手册.md#三阶段二启动音乐播放)。

在 Windows 项目根目录执行：

```powershell
python pc/music_sender/sender.py 192.168.7.2 9000 "C:\Music\example.mp3"
```

将地址、端口和路径替换为实际值。测试拆开发送：

```powershell
python pc/music_sender/sender.py 192.168.7.2 9000 "C:\Music\example.mp3" --fragment-size 3
```

多次发送可能被 TCP 合并，拆分模式用于增加测试覆盖，不能保证对端每次接收都被拆分。`--timeout` 控制连接和发送期间的单次操作超时；发送结束后等待播放结果不设超时。成功表示网关已播放完提交的 PCM。请关闭占用声卡的其他播放器。协议版本 2 需要发送端与网关同时更新。
