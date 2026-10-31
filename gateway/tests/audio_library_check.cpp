// 独立检查 mpg123 与 ALSA 的基本链接和运行，不打开音频设备。
#include <alsa/asoundlib.h>
#include <mpg123.h>

#include <iostream>

// 初始化解码库并创建解码器，最后按相反顺序释放资源。
int main()
{
    std::cout << "ALSA library: " << snd_asoundlib_version() << std::endl;
    const int init = mpg123_init();
    if (init != MPG123_OK) {
        std::cerr << "mpg123_init failed: " << mpg123_plain_strerror(init) << std::endl;
        return 1;
    }

    int error = MPG123_OK;
    mpg123_handle *decoder = mpg123_new(nullptr, &error);
    if (!decoder) {
        std::cerr << "mpg123_new failed: " << mpg123_plain_strerror(error) << std::endl;
        mpg123_exit();
        return 1;
    }

    std::cout << "mpg123 initialization and decoder creation succeeded" << std::endl;
    mpg123_delete(decoder);
    mpg123_exit();
    return 0;
}
