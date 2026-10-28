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

int main()
{
    const RuntimeInfo info = queryRuntimeInfo();
    printBanner(info);

    // 阶段一任务 5 完成后，这里改为：初始化日志 -> 读取配置 -> 写启动日志
    std::cout << "[骨架] 环境自检完成，日志模块与业务模块尚未接入。" << std::endl;
    return 0;
}
