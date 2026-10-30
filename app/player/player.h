#pragma once

namespace player {
    void cleanup();
    void open(const std::wstring& dir, double t);
    void play();
    void pause();
    void seek(double t);
    void close();
    void rate(double v);
    void step(int dir);
    void place(double x, double y, double w, double h);
}
