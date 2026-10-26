#include "pch.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>

#include "demos/reader.h"
#include "demos/talk.h"
#include "demos/track.h"
#include "demos/voice.h"
#include "json/json.h"
#include "parser/demo/demo.h"

namespace {
    int g_failed = 0;
    int g_passed = 0;

    void pass(const char* name) {
        g_passed++;
        printf("  ok    %s\n", name);
    }

    void fail(const char* name, const char* fmt, ...) {
        g_failed++;
        char text[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        printf("  FAIL  %s: %s\n", name, text);
    }

    void expect(bool ok, const char* name, const char* fmt, ...) {
        if (ok) {
            pass(name);
            return;
        }
        g_failed++;
        char text[512];
        va_list args;
        va_start(args, fmt);
        vsnprintf(text, sizeof(text), fmt, args);
        va_end(args);
        printf("  FAIL  %s: %s\n", name, text);
    }

    size_t private_bytes() {
        PROCESS_MEMORY_COUNTERS_EX pmc = {};
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
            return (size_t)pmc.PrivateUsage;
        return 0;
    }

    void check_json() {
        const std::string text = "{\"type\":\"set\",\"key\":\"talk\",\"value\":\"1\",\"list\":[1,2,3],\"flag\":true}";
        json_t v;
        std::string err;
        expect(json::parse(text, v, err), "json parses", "%s", err.c_str());
        expect(json::view(json::at(v, "type")) == "set", "json string field", "got %.20s", json::view(json::at(v, "type")).data());
        expect(json::view(json::at(v, "missing")) == "", "json missing field is empty", "");
        expect(json::num(json::at(v, "flag")) == 0, "json bool is not a number", "");
        const json_t* list = json::at(v, "list");
        expect(list && list->kind == jk_arr && list->arr.size() == 3, "json array", "size mismatch");
        expect(json::escape("a\"b") == "a\\\"b", "json escape", "got %s", json::escape("a\"b").c_str());
    }

    void check_demo(const std::string& path) {
        demo_t d;
        if (!demo::open(d, path.c_str())) {
            fail("demo opens", "%s", d.err.c_str());
            return;
        }
        pass("demo opens");
        expect(d.info.map.size() > 0, "demo has a map", "%s", d.info.map.c_str());

        frame_t f;
        size_t frames = 0;
        int last = 0;
        bool ordered = true;
        while (demo::next(d, f)) {
            frames++;
            if (f.cmd != dem_packet)
                continue;
            if (f.tick < last)
                ordered = false;
            last = f.tick;
        }
        expect(frames > 1000, "demo has frames", "%zu", frames);
        expect(ordered, "frame ticks are ordered", "");
        demo::close(d);
    }

    bool check_voice(const std::string& path) {
        std::vector<voice::spurt_t> spurts;
        std::vector<voice::round_mark_t> rounds;
        std::string err;
        if (!voice::spurts(path, 128, spurts, rounds, err)) {
            fail("voice spurts", "%s", err.c_str());
            return false;
        }
        if (spurts.empty()) {
            printf("  skip  voice: no voice in this demo\n");
            return false;
        }
        bool sane = true;
        for (const voice::spurt_t& s : spurts)
            if (s.start > s.end || !s.xuid)
                sane = false;
        pass("voice has spurts");
        expect(sane, "spurts are well formed", "");
        bool marks = !rounds.empty();
        for (const voice::round_mark_t& m : rounds)
            if (m.live >= 0 && m.live < m.prestart)
                marks = false;
        expect(marks, "round marks are ordered", "%zu rounds", rounds.size());
        return true;
    }

    void check_reader(const std::string& path) {
        roster_t r;
        if (!reader::read(path, r)) {
            fail("roster reads", "%s", r.err.c_str());
            return;
        }
        expect(!r.players.empty(), "roster has players", "%zu", r.players.size());
        bool stats = true;
        for (const roster_player_t& p : r.players)
            if (p.stats.kills < 0 || p.stats.deaths < 0)
                stats = false;
        expect(stats, "player stats are sane", "");
    }

    void check_talk(const std::string& path, const std::string& tmp) {
        roster_t r;
        if (reader::read(path, r) && r.rounds_info.size() > 12) {
            size_t swapped = 0;
            for (const talk::talker_t& t : talk::talkers(r))
                for (size_t i = 1; i < t.sides.size(); i++)
                    if (t.sides[i].second != t.sides[i - 1].second) {
                        swapped++;
                        break;
                    }
            expect(swapped > 0, "talkers follow the side swap", "%zu of %zu swapped", swapped, r.players.size());
        }

        talk::opts_t opts;
        std::string err;
        if (!talk::annotate(path, tmp, opts, err)) {
            fail("talk annotates", "%s", err.c_str());
            return;
        }
        pass("talk annotates");

        FILE* f = fopen(tmp.c_str(), "rb");
        if (!f) {
            fail("talk output opens", "cant open");
            return;
        }
        fseek(f, 0, SEEK_END);
        long long size = _ftelli64(f);
        uint8_t head[16] = {};
        fseek(f, 0, SEEK_SET);
        size_t got = fread(head, 1, 16, f);
        fclose(f);
        expect(got == 16 && size > 16, "talk output is a demo", "%lld bytes", size);
        if (got == 16) {
            uint32_t a = 0, b = 0;
            memcpy(&a, head + 8, 4);
            memcpy(&b, head + 12, 4);
            expect(a < size && b < size, "talk header offsets are inside the file", "%u %u of %lld", a, b, size);
        }
        remove(tmp.c_str());
    }

    void check_leaks(const std::string& path, int iters) {
        size_t base = 0;
        for (int i = 0; i < iters; i++) {
            roster_t r;
            reader::read(path, r);
            std::string blob, err;
            voice::extract(path, blob, err);
            size_t now = private_bytes();
            if (i == 0)
                base = now;
            printf("        leak probe %d: %.1f MB\n", i + 1, now / (1024.0 * 1024.0));
        }
        size_t end = private_bytes();
        const size_t limit = 24u * 1024 * 1024;
        expect(end < base + limit, "no unbounded growth across runs", "%.1f MB -> %.1f MB", base / (1024.0 * 1024.0),
               end / (1024.0 * 1024.0));
    }
}

int main(int argc, char** argv) {
    printf("cs2voice checks\n");
    check_json();

    if (argc < 2) {
        printf("\n  (no demo given, skipping demo checks)\n");
    } else {
        const std::string path = argv[1];
        const std::string tmp = path + ".checks-talk.dem";
        check_demo(path);
        check_reader(path);
        if (check_voice(path))
            check_talk(path, tmp);
        check_leaks(path, argc > 2 ? atoi(argv[2]) : 2);
    }

    printf("\n  %d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}