# TCP 音乐发送端

使用 Python 3 标准库，无需额外安装依赖。先在开发板启动接收服务，例如：

```sh
./edgelink --music-port 9000
```

在 Windows 项目根目录执行：

```powershell
python pc/music_sender/sender.py 192.168.7.2 9000 "C:\Music\example.mp3"
```

将地址、端口和路径替换为实际值。测试拆开发送：

```powershell
python pc/music_sender/sender.py 192.168.7.2 9000 "C:\Music\example.mp3" --fragment-size 3
```

多次发送可能被 TCP 合并，拆分模式用于增加测试覆盖，不能保证对端每次接收都被拆分。`--timeout` 控制连接和发送期间的单次操作超时；发送结束后等待播放结果不设超时。成功表示网关已播放完提交的 PCM。请关闭占用声卡的其他播放器。协议版本 2 需要发送端与网关同时更新。
