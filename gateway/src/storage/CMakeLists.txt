if(CMAKE_CROSSCOMPILING)
    # Debian multiarch 下显式选用 armhf 链接库。
    find_path(SQLite3_INCLUDE_DIR sqlite3.h
        PATHS /usr/include/arm-linux-gnueabihf /usr/include NO_DEFAULT_PATH)
    find_library(SQLite3_LIBRARY sqlite3 PATHS /usr/lib/arm-linux-gnueabihf NO_DEFAULT_PATH)
else()
    find_path(SQLite3_INCLUDE_DIR sqlite3.h)
    find_library(SQLite3_LIBRARY sqlite3)
endif()
if(NOT SQLite3_INCLUDE_DIR OR NOT SQLite3_LIBRARY)
    message(FATAL_ERROR "SQLite 存储需要 libsqlite3-dev，交叉构建使用 :armhf 版本")
endif()

add_library(edgelink_sqlite INTERFACE)
target_include_directories(edgelink_sqlite SYSTEM INTERFACE ${SQLite3_INCLUDE_DIR})
target_link_libraries(edgelink_sqlite INTERFACE ${SQLite3_LIBRARY})

add_library(edgelink_device_store STATIC device_store.cpp)
target_include_directories(edgelink_device_store PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(edgelink_device_store PRIVATE edgelink_sqlite)
target_compile_options(edgelink_device_store PRIVATE -Wall -Wextra)

add_executable(edgelink_db storage_main.cpp)
target_link_libraries(edgelink_db PRIVATE edgelink_device_store)
target_compile_options(edgelink_db PRIVATE -Wall -Wextra)
install(TARGETS edgelink_db RUNTIME DESTINATION bin)

if(EDGELINK_BUILD_STORAGE_CHECKS)
    add_executable(device_store_check ../../tests/device_store_check.cpp)
    target_link_libraries(device_store_check PRIVATE edgelink_device_store edgelink_sqlite)
    add_test(NAME device_store_check COMMAND device_store_check)
endif()
