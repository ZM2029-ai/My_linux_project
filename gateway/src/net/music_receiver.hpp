#pragma once

#include <cstdint>

// 阻塞运行，逐个处理连接；返回非零表示监听失败。
int runMusicReceiver(std::uint16_t port);
