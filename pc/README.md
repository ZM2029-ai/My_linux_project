# pc —— Windows 侧工具（运行在开发板之外）

这里的代码**在 Windows 上运行**，通过网络与开发板交互。不需要交叉编译。

## 计划中的模块

**一个功能一个文件夹**，在实现该功能时创建：

| 文件夹 | 对应阶段 | 作用 |
| --- | --- | --- |
| `music_sender/` | 阶段二 | 读取本地 MP3，通过 TCP 持续发送给开发板播放 |
| `sensor_sim/` | 阶段三 | 模拟物联网设备，定时发布 MQTT 遥测数据、接收控制命令 |

## 约定

1. **Python 3**，每个功能文件夹自成一体，可独立运行（`python -m` 或 `python xxx.py`）。
2. 依赖统一记录在 `requirements.txt`（按需创建），建议使用虚拟环境：
   ```bash
   python -m venv .venv
   .venv\Scripts\activate
   pip install -r requirements.txt
   ```
3. **开发板地址等参数通过命令行参数或环境变量传入**，不硬编码在代码里（默认值可写在文件夹内的 README）。
4. 与开发板交互的协议（MQTT 主题、JSON 字段、TCP 帧格式）以 [`protocol/`](../protocol/README.md) 为准，不在本目录另行定义。

## 开发板连接信息

| 项目 | 值 |
| --- | --- |
| IP（WiFi） | `10.192.110.142` |
| IP（USB 网卡） | `192.168.7.2` |
| SSH 用户 | `debian` |

> 密码不写入仓库。

## 当前状态

尚未开始编码（阶段二/三会分别创建对应文件夹）。
