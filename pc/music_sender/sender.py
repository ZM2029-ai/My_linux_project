"""按 EdgeLink 音乐流协议发送本地 MP3 文件。"""

import argparse
import socket
import struct
import sys
from pathlib import Path

HEADER = struct.Struct("!4sBBH")
MAX_PAYLOAD = 4096


def send_message(sock, message_type, payload=b"", fragment_size=0):
    """发送一条消息；可将字节拆开发送以辅助测试接收端。"""
    message = HEADER.pack(b"ELMS", 2, message_type, len(payload)) + payload
    size = fragment_size or len(message)
    for offset in range(0, len(message), size):
        sock.sendall(message[offset:offset + size])


def receive_exact(sock, size):
    """循环读取指定字节数，处理 TCP 分段和提前断线。"""
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("网关在返回完整结果前关闭连接")
        data.extend(chunk)
    return bytes(data)


def send_file(host, port, path, fragment_size, timeout):
    """逐块发送文件并校验网关的 RESULT 消息。"""
    total = 0
    with path.open("rb") as source:
        # 空文件不满足协议要求的至少一条 DATA，提前报错。
        first = source.read(MAX_PAYLOAD)
        if not first:
            raise ValueError("文件为空，无法发送")
        with socket.create_connection((host, port), timeout=timeout) as sock:
            send_message(sock, 1, b"MP3", fragment_size)
            chunk = first
            while chunk:
                send_message(sock, 2, chunk, fragment_size)
                total += len(chunk)
                chunk = source.read(MAX_PAYLOAD)
            send_message(sock, 3, fragment_size=fragment_size)
            # 播放可能持续数分钟；等待结果不使用短暂的操作超时。
            sock.settimeout(None)
            # 先检查固定长度的响应头，再读取状态字节。
            magic, version, kind, length = HEADER.unpack(receive_exact(sock, HEADER.size))
            if (magic, version, kind, length) != (b"ELMS", 2, 4, 1):
                raise ValueError("网关返回了无效的 RESULT 消息")
            status = receive_exact(sock, 1)[0]
            if status != 0:
                raise ValueError("网关报告错误状态：{}".format(status))
    return total


def main():
    """解析运行参数，报告发送结果和常见错误。"""
    parser = argparse.ArgumentParser(description="通过 TCP 向 EdgeLink 网关发送 MP3")
    parser.add_argument("host", help="开发板地址")
    parser.add_argument("port", type=int, help="网关监听端口")
    parser.add_argument("file", type=Path, help="本地 MP3 文件路径")
    parser.add_argument("--fragment-size", type=int, default=0,
                        help="将消息拆成指定字节数发送，0 表示不主动拆分")
    parser.add_argument("--timeout", type=float, default=30,
                        help="连接及单次套接字操作超时秒数，默认 30")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or args.fragment_size < 0 or args.timeout <= 0:
        parser.error("端口应为 1～65535，分段大小不能为负，超时必须大于 0")
    try:
        total = send_file(args.host, args.port, args.file, args.fragment_size, args.timeout)
    except (OSError, ValueError) as exc:
        print("发送失败：{}".format(exc), file=sys.stderr)
        return 1
    print("网关播放完成：{} 字节".format(total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
