#include "music_receiver.hpp"
#include "audio/music_player.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {
// 循环接收指定字节数，处理 TCP 分段和被信号中断的系统调用。
// 连接关闭或接收失败时返回 false。
bool readExact(int fd, void *buffer, std::size_t size)
{
    auto *p = static_cast<unsigned char *>(buffer);
    while (size) {
        const auto n = recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

// 按协议发送 RESULT，处理一次 send 未写完全部字节的情况。
void result(int fd, unsigned char status)
{
    const unsigned char msg[] = {'E','L','M','S',2,4,0,1,status};
    std::size_t sent = 0;
    while (sent < sizeof(msg)) {
        // 对端已断开时避免 SIGPIPE 终止整个网关。
        const auto n = send(fd, msg + sent, sizeof(msg) - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

// 处理单个连接：同步解码播放，慢消费由 TCP 背压限制发送速度。
void receiveStream(int fd)
{
    MusicPlayer player;
    unsigned char header[8], payload[4096];
    bool started = false, hasData = false;
    std::uint64_t total = 0;
    for (;;) {
        if (!readExact(fd, header, sizeof(header))) {
            std::cerr << "[music] connection interrupted\n";
            result(fd, 2);
            return;
        }
        // 协议头中的两字节长度采用网络字节序。
        const unsigned type = header[5];
        const unsigned length = (static_cast<unsigned>(header[6]) << 8) | header[7];
        // 在读取载荷前校验长度和状态，防止越界及非法消息顺序。
        if (std::memcmp(header, "ELMS", 4) != 0 || header[4] != 2 || length > sizeof(payload) ||
            (!started && (type != 1 || length != 3)) ||
            (started && !((type == 2 && length > 0) || (type == 3 && length == 0 && hasData)))) {
            std::cerr << "[music] invalid message\n";
            result(fd, 1);
            return;
        }
        if (!readExact(fd, payload, length)) {
            std::cerr << "[music] incomplete payload\n";
            result(fd, 2);
            return;
        }
        // START 确认格式，DATA 累计长度，END 完成当前连接。
        if (type == 1) {
            if (std::memcmp(payload, "MP3", 3) != 0) {
                result(fd, 1);
                return;
            }
            if (!player.open()) {
                std::cerr << "[music] " << player.error() << std::endl;
                result(fd, 3);
                return;
            }
            started = true;
            std::cout << "[music] stream started" << std::endl;
        } else if (type == 2) {
            if (!player.feed(payload, length)) {
                std::cerr << "[music] " << player.error() << std::endl;
                result(fd, 3);
                return;
            }
            total += length;
            hasData = true;
        } else {
            std::cout << "[music] received " << total << " bytes" << std::endl;
            if (!player.finish()) {
                std::cerr << "[music] " << player.error() << std::endl;
                result(fd, 3);
                return;
            }
            std::cout << "[music] playback complete" << std::endl;
            result(fd, 0);
            return;
        }
    }
}
}

// 创建监听套接字并串行处理客户端，监听失败时返回非零值。
int runMusicReceiver(std::uint16_t port)
{
    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { std::perror("socket"); return 1; }
    int yes = 1;
    // 允许服务重启时尽快重新绑定端口。
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (bind(server, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 || listen(server, 1) < 0) {
        std::perror("bind/listen"); close(server); return 1;
    }
    std::cout << "[music] listening on port " << port << std::endl;
    // 每次只处理一个连接，结束后继续等待下一位客户端。
    for (;;) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::perror("accept"); close(server); return 1;
        }
        receiveStream(client);
        close(client);
    }
}
