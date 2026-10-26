#include "pch.h"

#include <windows.h>

#include <chrono>
#include <cstdio>

#include "demos/reader.h"
#include "demos/talk.h"
#include "demos/track.h"
#include "demos/voice.h"
#include "parser/demo/demo.h"

using clk = std::chrono::steady_clock;

struct cpu_t {
    unsigned long long idle = 0;
    unsigned long long kernel = 0;
    unsigned long long user = 0;
};

static cpu_t cpu_now() {
    cpu_t c;
    FILETIME i, k, u;
    if (GetSystemTimes(&i, &k, &u)) {
        c.idle = ((unsigned long long)i.dwHighDateTime << 32) | i.dwLowDateTime;
        c.kernel = ((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime;
        c.user = ((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime;
    }
    return c;
}

static double busy_percent(const cpu_t& a, const cpu_t& b) {
    unsigned long long idle = b.idle - a.idle;
    unsigned long long total = (b.kernel - a.kernel) + (b.user - a.user);
    if (!total)
        return 0;
    return 100.0 * (1.0 - (double)idle / (double)total);
}

struct result_t {
    std::string name;
    double ms = 0;
};

static double millis(clk::time_point a, clk::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static double walk(const std::string& path, size_t& frames, std::string& err) {
    auto t0 = clk::now();
    demo_t d;
    if (!demo::open(d, path.c_str())) {
        err = d.err;
        return -1;
    }
    frame_t f;
    frames = 0;
    while (demo::next(d, f))
        frames++;
    demo::close(d);
    return millis(t0, clk::now());
}

static void row(const result_t& r, double bytes) {
    const double mb = bytes / (1024.0 * 1024.0);
    printf("  %-22s %9.1f ms  %7.1f MB/s\n", r.name.c_str(), r.ms, r.ms > 0 ? mb / (r.ms / 1000.0) : 0);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: bench <demo> [runs]\n");
        return 2;
    }
    const std::string path = argv[1];
    const int runs = argc > 2 ? atoi(argv[2]) : 3;

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        printf("cant open %s\n", path.c_str());
        return 1;
    }
    fseek(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    fclose(f);

    printf("cs2voice bench\n");
    printf("  %s (%.1f MB, %d runs, best)\n\n", path.c_str(), size / (1024.0 * 1024.0), runs);

    cpu_t cpu_before = cpu_now();
    double best_walk = 1e30, best_reader = 1e30, best_voice = 1e30, best_track = 1e30, best_talk = 1e30;
    size_t frames = 0;

    for (int i = 0; i < runs; i++) {
        std::string err;
        double ms = walk(path, frames, err);
        if (ms < 0) {
            printf("  walk failed: %s\n", err.c_str());
            return 1;
        }
        best_walk = std::min(best_walk, ms);

        {
            roster_t r;
            auto t0 = clk::now();
            bool ok = reader::read(path, r);
            double m = millis(t0, clk::now());
            if (!ok) {
                printf("  reader failed: %s\n", r.err.c_str());
                return 1;
            }
            best_reader = std::min(best_reader, m);
        }

        {
            std::string blob;
            auto t0 = clk::now();
            bool ok = voice::extract(path, blob, err);
            double m = millis(t0, clk::now());
            if (!ok) {
                printf("  voice failed: %s\n", err.c_str());
                return 1;
            }
            best_voice = std::min(best_voice, m);
        }

        {
            std::string blob;
            auto t0 = clk::now();
            bool ok = track::extract(path, blob, err);
            double m = millis(t0, clk::now());
            if (!ok) {
                printf("  track failed: %s\n", err.c_str());
                return 1;
            }
            best_track = std::min(best_track, m);
        }

        {
            std::string out = path + ".bench-talk.dem";
            auto t0 = clk::now();
            bool ok = talk::annotate(path, out, talk::opts_t(), err);
            double m = millis(t0, clk::now());
            remove(out.c_str());
            if (!ok) {
                printf("  talk failed: %s\n", err.c_str());
                return 1;
            }
            best_talk = std::min(best_talk, m);
        }
    }

    row({"open + walk frames", best_walk}, (double)size);
    row({"reader::read", best_reader}, (double)size);
    row({"voice::extract", best_voice}, (double)size);
    row({"track::extract", best_track}, (double)size);
    row({"talk::annotate", best_talk}, (double)size);
    row({"total (all stages)", best_reader + best_voice + best_track + best_talk}, (double)size);

    printf("\n  frames %zu, %.0f frames/s\n", frames, frames / (best_walk / 1000.0));

    double busy = busy_percent(cpu_before, cpu_now());
    printf("  cpu busy during run %.0f%%", busy);
    if (busy > 20)
        printf("  <- machine is loaded, numbers are not representative");
    printf("\n");
    return 0;
}
