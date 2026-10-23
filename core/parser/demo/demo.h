#pragma once

#include "parser/demo/zstdx.h"

#include <atomic>
#include <thread>

enum demo_cmd_t {
    dem_stop = 0,
    dem_file_header = 1,
    dem_file_info = 2,
    dem_sync_tick = 3,
    dem_send_tables = 4,
    dem_class_info = 5,
    dem_string_tables = 6,
    dem_packet = 7,
    dem_signon_packet = 8,
    dem_console_cmd = 9,
    dem_custom_data = 10,
    dem_custom_data_callbacks = 11,
    dem_user_cmd = 12,
    dem_full_packet = 13,
    dem_save_game = 14,
    dem_spawn_groups = 15,
    dem_animation_data = 16,
    dem_animation_header = 17,
};

struct frame_t {
    int cmd = 0;
    int tick = 0;
    bool compressed = false;
    const uint8_t* data = nullptr;
    size_t len = 0;
};

struct demo_info_t {
    std::string stamp;
    std::string server;
    std::string client;
    std::string map;
    std::string game_dir;
    std::string version;
    int32_t protocol = 0;
    int32_t build = 0;
};

struct demo_t {
    zstdx::buffer_t buf;
    const uint8_t* data = nullptr;
    const uint8_t* src = nullptr;
    size_t src_size = 0;
    void* file = nullptr;
    void* mapping = nullptr;
    const void* view = nullptr;
    std::thread* unpacker = nullptr;
    std::atomic<size_t> ready = 0;
    std::atomic<bool> stop = false;
    std::string unpack_err;
    std::vector<uint8_t> unpacked;
    size_t size = 0;
    size_t first = 0;
    size_t pos = 0;
    size_t packed_size = 0;
    demo_info_t info;
    std::string err;
};

namespace demo {
    bool open(demo_t& d, const char* path);
    bool next(demo_t& d, frame_t& f);
    bool last_full(demo_t& d, size_t& at);
    void rewind(demo_t& d);
    void close(demo_t& d);
}
