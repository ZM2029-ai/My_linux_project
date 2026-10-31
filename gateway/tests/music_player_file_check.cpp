#include "audio/music_player.hpp"
#include <cstdio>
#include <iostream>

// 本地分块读取 MP3，验证正式模块的打开、输入、结束和错误处理。
int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: music_player_file_check FILE [ALSA_DEVICE]" << std::endl;
        return 1;
    }
    FILE *file = std::fopen(argv[1], "rb");
    if (!file) { std::perror("open file"); return 1; }
    MusicPlayer player;
    bool ok = player.open(argc == 3 ? argv[2] : "default");
    unsigned char input[4096];
    while (ok) {
        const size_t count = std::fread(input, 1, sizeof(input), file);
        if (count && !player.feed(input, count)) { ok = false; break; }
        if (count < sizeof(input)) {
            if (std::ferror(file)) { std::perror("read file"); ok = false; }
            break;
        }
    }
    if (ok) ok = player.finish();
    if (!ok && !player.error().empty()) std::cerr << player.error() << std::endl;
    std::fclose(file);
    if (ok) std::cout << "Playback complete" << std::endl;
    return ok ? 0 : 1;
}
