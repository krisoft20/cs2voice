#pragma once

struct rec_video_t {
    std::string folder;
    double origin = 0;
    double start = 0;
    double duration = 0;
    uint64_t bytes = 0;
};

struct rec_match_t {
    std::string mode;
    std::string map;
    std::string kda;
    std::string score;
    double start = 0;
    double duration = 0;
};

struct rec_event_t {
    double t = 0;
    std::string kind;
    std::string icon;
    std::string title;
    std::string text;
    int id = 0;
};

struct rec_timeline_t {
    std::string name;
    int app = 0;
    int64_t unix = 0;
    double duration = 0;
    std::vector<rec_video_t> videos;
    std::vector<rec_match_t> matches;
    std::vector<rec_event_t> marks;
};

namespace recordings {
    bool index(const std::string& pb, std::vector<rec_timeline_t>& out);
    bool events(const std::string& json, std::vector<rec_event_t>& out);
    void extend(std::vector<rec_match_t>& list, const std::vector<rec_event_t>& events);
}
