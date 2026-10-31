// 本地文件分块解码测试：不依赖 TCP，使用 ALSA 播放解码后的 PCM。
#include <alsa/asoundlib.h>
#include <mpg123.h>
#include <cstdio>
#include <iostream>

// 将 PCM 全部写入设备，并恢复缓冲区欠载等可恢复错误。
bool writePcm(snd_pcm_t *pcm, const unsigned char *data, size_t bytes, int channels)
{
    const size_t frameBytes = static_cast<size_t>(channels) * 2;
    if (bytes % frameBytes) return false;
    snd_pcm_uframes_t frames = bytes / frameBytes;
    while (frames) {
        const snd_pcm_sframes_t n = snd_pcm_writei(pcm, data, frames);
        if (n < 0) {
            const int rc = snd_pcm_recover(pcm, static_cast<int>(n), 1);
            if (rc < 0) { std::cerr << "ALSA write: " << snd_strerror(rc) << std::endl; return false; }
        } else if (n == 0) {
            std::cerr << "ALSA write made no progress" << std::endl;
            return false;
        } else {
            data += static_cast<size_t>(n) * frameBytes;
            frames -= static_cast<snd_pcm_uframes_t>(n);
        }
    }
    return true;
}

// 从解码器取出可用音频；格式变化时重新配置 ALSA。
bool drainDecoder(mpg123_handle *decoder, snd_pcm_t *pcm, int &channels, bool &configured, bool &decoded)
{
    unsigned char output[16384];
    for (;;) {
        size_t done = 0;
        const int rc = mpg123_read(decoder, output, sizeof(output), &done);
        if (rc == MPG123_NEW_FORMAT) {
            long rate = 0;
            int encoding = 0;
            if (mpg123_getformat(decoder, &rate, &channels, &encoding) != MPG123_OK ||
                encoding != MPG123_ENC_SIGNED_16 || channels < 1 || rate <= 0) {
                std::cerr << "Unsupported decoder output format" << std::endl;
                return false;
            }
            if (configured && snd_pcm_drain(pcm) < 0) return false;
            const int err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
                                               static_cast<unsigned>(channels), static_cast<unsigned>(rate), 1, 500000);
            if (err < 0) { std::cerr << "ALSA setup: " << snd_strerror(err) << std::endl; return false; }
            configured = true;
            std::cout << "PCM: " << rate << " Hz, " << channels << " channel(s), signed 16-bit" << std::endl;
        }
        if (done) {
            if (!configured || !writePcm(pcm, output, done, channels)) return false;
            decoded = true;
        }
        if (rc == MPG123_NEED_MORE || rc == MPG123_DONE) return true;
        if (rc != MPG123_OK && rc != MPG123_NEW_FORMAT) {
            std::cerr << "Decode: " << mpg123_strerror(decoder) << std::endl;
            return false;
        }
    }
}

// 打开本地文件和音频资源，分块送入解码器，播放结束后释放资源。
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) { std::cerr << "Usage: audio_file_play FILE [ALSA_DEVICE]" << std::endl; return 1; }
    FILE *file = std::fopen(argv[1], "rb");
    if (!file) { std::perror("open file"); return 1; }
    if (mpg123_init() != MPG123_OK) { std::fclose(file); return 1; }
    int error = 0;
    mpg123_handle *decoder = mpg123_new(nullptr, &error);
    snd_pcm_t *pcm = nullptr;
    bool ok = decoder && mpg123_open_feed(decoder) == MPG123_OK;
    if (ok) {
        error = snd_pcm_open(&pcm, argc == 3 ? argv[2] : "default", SND_PCM_STREAM_PLAYBACK, 0);
        if (error < 0) { std::cerr << "ALSA open: " << snd_strerror(error) << std::endl; ok = false; }
    }
    unsigned char input[4096];
    int channels = 0;
    bool configured = false, decoded = false;
    while (ok) {
        const size_t count = std::fread(input, 1, sizeof(input), file);
        if (count && (mpg123_feed(decoder, input, count) != MPG123_OK ||
                      !drainDecoder(decoder, pcm, channels, configured, decoded))) { ok = false; break; }
        if (count < sizeof(input)) { if (std::ferror(file)) ok = false; break; }
    }
    if (!decoded) { std::cerr << "No audio decoded" << std::endl; ok = false; }
    if (pcm) {
        if (ok && snd_pcm_drain(pcm) < 0) ok = false;
        snd_pcm_close(pcm);
    }
    if (decoder) mpg123_delete(decoder);
    mpg123_exit();
    std::fclose(file);
    if (ok) std::cout << "Playback complete" << std::endl;
    return ok ? 0 : 1;
}
