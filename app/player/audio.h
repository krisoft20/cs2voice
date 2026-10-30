#pragma once

namespace audio {
    bool open(const std::wstring& dir, double origin, std::string& err);
    void close();
    void start(double t);
    void stop();
    bool clock(double& t);
    void volume(float v);
}
