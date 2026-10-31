// EdgeLink 网关入口程序
//
// 阶段一（骨架版本）：只做环境自检与启动横幅打印。
// 后续阶段在此逐步接入：日志模块 -> 配置模块 -> TCP/MQTT -> SQLite -> HTTP API。
//
// 运行目标：i.MX6ULL 开发板（Debian 10 / armhf / glibc 2.28）
// 编译方式：GCC 8.3 交叉编译，见 gateway/cmake/toolchain-arm-linux-gnueabihf.cmake

#include <gnu/libc-version.h>
#include <sys/utsname.h>

#include <iostream>
#include <string>
#include <thread>
#include <cstdlib>
#include <cerrno>
#include "net/music_receiver.hpp"

#ifndef EDGELINK_VERSION
#define EDGELINK_VERSION "unknown"
#endif

namespace {

struct RuntimeInfo {
    std::string arch;      // CPU 架构，例如 armv7l
    std::string kernel;    // 内核版本，例如 4.19.35-imx6
    std::string glibc;     // C 运行库版本，例如 2.28
    unsigned hwThreads;    // 硬件线程数
};

RuntimeInfo queryRuntimeInfo()
{
    RuntimeInfo info{"unknown", "unknown", "unknown", 0};

    utsname sysInfo{};
    if (uname(&sysInfo) == 0) {
        info.arch = sysInfo.machine;
        info.kernel = sysInfo.release;
    }

    // glibc 专有接口，仅用于启动自检输出
    info.glibc = gnu_get_libc_version();

    const unsigned threads = std::thread::hardware_concurrency();
    info.hwThreads = threads > 0 ? threads : 1;

    return info;
}

void printBanner(const RuntimeInfo &info)
{
    std::cout << "==============================\n"
              << " EdgeLink gateway\n"
              << "==============================\n"
              << " version   : " << EDGELINK_VERSION << "\n"
              << " arch      : " << info.arch << "\n"
              << " kernel    : " << info.kernel << "\n"
              << " glibc     : " << info.glibc << "\n"
              << " cores     : " << info.hwThreads << "\n"
              << " built at  : " << __DATE__ << " " << __TIME__ << "\n"
              << "------------------------------\n"
              << std::endl;
}

}  // namespace

// 打印环境信息；指定音乐端口时启动接收服务，否则保留原有自检行为。
int main(int argc, char **argv)
{
    const RuntimeInfo info = queryRuntimeInfo();
    printBanner(info);

    if (argc == 3 && std::string(argv[1]) == "--music-port") {
        char *end = nullptr;
        errno = 0;
        // 检查完整输入和合法端口范围，避免截断或接受无效字符。
        const long port = std::strtol(argv[2], &end, 10);
        if (errno || end == argv[2] || *end || port < 1 || port > 65535) {
            std::cerr << "Invalid port (1-65535)" << std::endl;
            return 1;
        }
        return runMusicReceiver(static_cast<std::uint16_t>(port));
    }
    if (argc != 1) {
        std::cerr << "Usage: edgelink [--music-port PORT]" << std::endl;
        return 1;
    }

    // 阶段一任务 5 完成后，这里改为：初始化日志 -> 读取配置 -> 写启动日志
    std::cout << "[骨架] 环境自检完成，日志模块与业务模块尚未接入。" << std::endl;
    return 0;
}
