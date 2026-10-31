#pragma once
#include <cstddef>
#include <memory>
#include <string>

// 一个实例负责一条 MP3 流；调用方需串行调用各方法。
class MusicPlayer {
public:
    MusicPlayer();
    ~MusicPlayer();
    MusicPlayer(const MusicPlayer &) = delete;
    MusicPlayer &operator=(const MusicPlayer &) = delete;
    // 打开解码器与 ALSA 设备；失败原因可通过 error() 获取。
    bool open(const std::string &device = "default");
    // 输入任意长度的 MP3 字节并同步播放当前可解码的数据。
    bool feed(const unsigned char *data, std::size_t size);
    // 正常结束并等待已提交的 PCM 播放完成。
    bool finish();
    // 中止播放并释放资源，可在任何状态下调用。
    void close();
    const std::string &error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
