#include "music_player.hpp"
#include <alsa/asoundlib.h>
#include <mpg123.h>

// 保存单条流的资源和状态，避免向调用方暴露第三方库类型。
struct MusicPlayer::Impl {
    mpg123_handle *decoder = nullptr;
    snd_pcm_t *pcm = nullptr;
    bool initialized = false, configured = false, decoded = false, failed = false;
    int channels = 0;
    std::string message;

    // 记录错误并阻止继续向已失败的流写入。
    bool fail(const std::string &text) { message = text; failed = true; return false; }

    // 处理 ALSA 部分写入及欠载恢复。
    bool write(const unsigned char *data, size_t bytes) {
        const size_t frameBytes = static_cast<size_t>(channels) * 2;
        if (bytes % frameBytes) return fail("PCM frame is incomplete");
        snd_pcm_uframes_t frames = bytes / frameBytes;
        while (frames) {
            const snd_pcm_sframes_t n = snd_pcm_writei(pcm, data, frames);
            if (n < 0) {
                const int rc = snd_pcm_recover(pcm, static_cast<int>(n), 1);
                if (rc < 0) return fail(std::string("ALSA write: ") + snd_strerror(rc));
            } else if (n == 0) return fail("ALSA write made no progress");
            else { data += static_cast<size_t>(n) * frameBytes; frames -= static_cast<snd_pcm_uframes_t>(n); }
        }
        return true;
    }

    // 消费已输入的数据；根据解码器格式配置 ALSA。
    bool decode() {
        unsigned char output[16384];
        for (;;) {
            size_t done = 0;
            const int rc = mpg123_read(decoder, output, sizeof(output), &done);
            if (rc == MPG123_NEW_FORMAT) {
                long rate = 0; int encoding = 0;
                if (mpg123_getformat(decoder, &rate, &channels, &encoding) != MPG123_OK ||
                    encoding != MPG123_ENC_SIGNED_16 || channels < 1 || rate <= 0)
                    return fail("Unsupported decoder output format");
                if (configured) {
                    const int err = snd_pcm_drain(pcm);
                    if (err < 0) return fail(std::string("ALSA drain: ") + snd_strerror(err));
                }
                const int err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
                    static_cast<unsigned>(channels), static_cast<unsigned>(rate), 1, 500000);
                if (err < 0) return fail(std::string("ALSA setup: ") + snd_strerror(err));
                configured = true;
            }
            if (done) {
                if (!configured) return fail("PCM output before format setup");
                if (!write(output, done)) return false;
                decoded = true;
            }
            if (rc == MPG123_NEED_MORE || rc == MPG123_DONE) return true;
            if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) return fail(std::string("Decode: ") + mpg123_strerror(decoder));
        }
    }
};

// 构造空播放器；资源由 open 显式获取。
MusicPlayer::MusicPlayer() : impl_(new Impl) {}
// 销毁时中止尚未结束的播放并释放资源。
MusicPlayer::~MusicPlayer() { close(); }

// 初始化解码器并打开音频设备，任何失败都会释放已获取的资源。
bool MusicPlayer::open(const std::string &device) {
    close(); impl_->message.clear(); impl_->failed = false;
    int rc = mpg123_init();
    if (rc != MPG123_OK) return impl_->fail(std::string("mpg123 init: ") + mpg123_plain_strerror(rc));
    impl_->initialized = true;
    impl_->decoder = mpg123_new(nullptr, &rc);
    if (!impl_->decoder) { impl_->fail(std::string("mpg123 new: ") + mpg123_plain_strerror(rc)); close(); return false; }
    rc = mpg123_open_feed(impl_->decoder);
    if (rc != MPG123_OK) { impl_->fail(std::string("mpg123 feed: ") + mpg123_plain_strerror(rc)); close(); return false; }
    rc = snd_pcm_open(&impl_->pcm, device.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) { impl_->fail(std::string("ALSA open: ") + snd_strerror(rc)); close(); return false; }
    return true;
}

// 分段输入 MP3 并同步消费当前可解码的 PCM。
bool MusicPlayer::feed(const unsigned char *data, std::size_t size) {
    if (!impl_->pcm || impl_->failed || (!data && size)) return impl_->fail("Player is not ready");
    if (!size) return true;
    if (mpg123_feed(impl_->decoder, data, size) != MPG123_OK) return impl_->fail(std::string("Feed: ") + mpg123_strerror(impl_->decoder));
    return impl_->decode();
}

// 完成已输入数据的播放，失败时保留错误供调用方读取。
bool MusicPlayer::finish() {
    if (!impl_->pcm || impl_->failed) return false;
    if (!impl_->decode()) return false;
    if (!impl_->decoded) return impl_->fail("No audio decoded");
    const int rc = snd_pcm_drain(impl_->pcm);
    if (rc < 0) return impl_->fail(std::string("ALSA drain: ") + snd_strerror(rc));
    close(); return true;
}

// 丢弃未播放的数据并释放 ALSA 与解码器资源。
void MusicPlayer::close() {
    if (impl_->pcm) { snd_pcm_drop(impl_->pcm); snd_pcm_close(impl_->pcm); impl_->pcm = nullptr; }
    if (impl_->decoder) { mpg123_delete(impl_->decoder); impl_->decoder = nullptr; }
    if (impl_->initialized) { mpg123_exit(); impl_->initialized = false; }
    impl_->configured = false; impl_->decoded = false; impl_->channels = 0;
}
// 返回最近一次失败的诊断信息。
const std::string &MusicPlayer::error() const { return impl_->message; }
