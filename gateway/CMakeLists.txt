cmake_minimum_required(VERSION 3.13)

# chroot 内 cmake 为 3.13.4，上面的版本号不能更高
project(edgelink VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

add_executable(edgelink
    src/main.cpp
    src/net/music_receiver.cpp
)

target_include_directories(edgelink PRIVATE src)

target_compile_definitions(edgelink PRIVATE
    EDGELINK_VERSION="${PROJECT_VERSION}"
)

# -O2/-g 由构建类型决定，这里只加警告
target_compile_options(edgelink PRIVATE -Wall -Wextra)

# 网关播放功能依赖目标板架构的音频开发库。
    # Debian multiarch 中明确选择目标板的 armhf 库，避免误用宿主库。
    find_path(MPG123_INCLUDE_DIR mpg123.h PATHS /usr/include/arm-linux-gnueabihf /usr/include NO_DEFAULT_PATH)
    find_path(ALSA_INCLUDE_DIR alsa/asoundlib.h PATHS /usr/include NO_DEFAULT_PATH)
    find_library(MPG123_LIBRARY mpg123 PATHS /usr/lib/arm-linux-gnueabihf NO_DEFAULT_PATH)
    find_library(ALSA_LIBRARY asound PATHS /usr/lib/arm-linux-gnueabihf NO_DEFAULT_PATH)
    if(NOT MPG123_INCLUDE_DIR OR NOT ALSA_INCLUDE_DIR OR NOT MPG123_LIBRARY OR NOT ALSA_LIBRARY)
        message(FATAL_ERROR "找不到 armhf mpg123 或 ALSA 开发文件")
    endif()
    add_library(music_player STATIC src/audio/music_player.cpp)
    target_include_directories(music_player PRIVATE ${MPG123_INCLUDE_DIR} ${ALSA_INCLUDE_DIR})
    target_link_libraries(music_player PUBLIC ${MPG123_LIBRARY} ${ALSA_LIBRARY})
    target_compile_options(music_player PRIVATE -Wall -Wextra)
    target_link_libraries(edgelink PRIVATE music_player)
option(EDGELINK_BUILD_AUDIO_CHECK "构建音频库检查程序" OFF)
if(EDGELINK_BUILD_AUDIO_CHECK)
    add_executable(audio_library_check tests/audio_library_check.cpp)
    target_include_directories(audio_library_check PRIVATE ${MPG123_INCLUDE_DIR} ${ALSA_INCLUDE_DIR})
    target_link_libraries(audio_library_check PRIVATE ${MPG123_LIBRARY} ${ALSA_LIBRARY})
    target_compile_options(audio_library_check PRIVATE -Wall -Wextra)
    add_executable(music_player_file_check tests/music_player_file_check.cpp)
    target_include_directories(music_player_file_check PRIVATE src)
    target_link_libraries(music_player_file_check PRIVATE music_player)
    target_compile_options(music_player_file_check PRIVATE -Wall -Wextra)
    add_executable(audio_file_play tests/audio_file_play.cpp)
    target_include_directories(audio_file_play PRIVATE ${MPG123_INCLUDE_DIR} ${ALSA_INCLUDE_DIR})
    target_link_libraries(audio_file_play PRIVATE ${MPG123_LIBRARY} ${ALSA_LIBRARY})
    target_compile_options(audio_file_play PRIVATE -Wall -Wextra)
endif()

install(TARGETS edgelink RUNTIME DESTINATION bin)
