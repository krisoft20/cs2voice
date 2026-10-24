#include "pch.h"

#include "bridge/launch.h"
#include "dbg.h"
#include "demos/basefix.h"
#include "json/json.h"
#include "parser/demo/zstdx.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"

namespace launch {
    struct job_t {
        demo_file_t file;
        uint32_t low = 0;
        uint32_t high = 0;
        bool fix = false;
        bool copy = true;
    };

    const int KEEP = 3;

    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_worker;
    std::atomic<bool> g_quit = false;
    std::wstring g_dir;
    job_t g_job;
    bool g_pending = false;
    bool g_clear = false;

    void post(const std::string& state, const std::string& extra) {
        window::post_json("{\"type\":\"watch\",\"state\":\"" + state + "\"" + extra + "}");
    }

    bool read_all(const std::wstring& path, std::vector<uint8_t>& out) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f)
            return false;
        _fseeki64(f, 0, SEEK_END);
        int64_t n = _ftelli64(f);
        _fseeki64(f, 0, SEEK_SET);
        out.resize(n > 0 ? (size_t)n : 0);
        bool ok = n > 0 && fread(out.data(), 1, out.size(), f) == out.size();
        fclose(f);
        return ok;
    }

    std::wstring index_path() {
        return g_dir + L"\\index.txt";
    }

    void write_index(const std::vector<std::string>& lines) {
        FILE* f = _wfopen(index_path().c_str(), L"wb");
        if (!f)
            return;
        for (const std::string& l : lines)
            fprintf(f, "%s\n", l.c_str());
        fclose(f);
    }

    std::vector<std::string> read_index() {
        std::vector<std::string> out;
        FILE* f = _wfopen(index_path().c_str(), L"rb");
        if (!f)
            return out;
        char line[2048];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            files::chomp(s);
            if (!s.empty())
                out.push_back(s);
        }
        fclose(f);
        return out;
    }

    long long stamp(const std::string& line) {
        return atoll(line.c_str() + line.rfind('\t') + 1);
    }

    void trim(int keep) {
        std::vector<std::string> lines = read_index(), kept;
        std::stable_sort(lines.begin(), lines.end(), [](const std::string& a, const std::string& b) { return stamp(a) > stamp(b); });
        std::set<std::string> known;
        int busy = 0;
        for (const std::string& l : lines) {
            std::string name = l.substr(0, l.find('\t'));
            std::wstring path = g_dir + L"\\" + text::wide(name);
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
                continue;
            bool young = (int)kept.size() < keep;
            if (young || !DeleteFileW(path.c_str())) {
                busy += !young;
                kept.push_back(l);
                known.insert(text::lower(name));
            }
        }
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((g_dir + L"\\*.dem*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do
                if (!known.count(text::lower(text::utf8(fd.cFileName))))
                    DeleteFileW((g_dir + L"\\" + fd.cFileName).c_str());
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        write_index(kept);
        if (busy)
            dbg::print("[launch] %d unpacked demos are still in use, will retry", busy);
    }

    void touch(const std::string& name) {
        std::vector<std::string> lines = read_index();
        for (std::string& l : lines)
            if (l.rfind(name + "\t", 0) == 0)
                l = l.substr(0, l.rfind('\t') + 1) + std::to_string((long long)time(nullptr));
        write_index(lines);
    }

    bool unpack(const demo_file_t& f, std::string& out, std::string& err) {
        std::string name = f.name.substr(0, f.name.size() - 4);
        std::wstring dst = g_dir + L"\\" + text::wide(name);
        out = text::utf8(dst);
        if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
            touch(name);
            return true;
        }
        std::vector<uint8_t> packed;
        zstdx::buffer_t plain;
        if (!read_all(text::wide(f.path), packed)) {
            err = "cant read the demo";
            return false;
        }
        size_t len = 0;
        if (!zstdx::decompress(packed.data(), packed.size(), plain, len, err))
            return false;
        trim(KEEP - 1);
        std::wstring tmp = dst + L".part";
        FILE* o = _wfopen(tmp.c_str(), L"wb");
        if (!o) {
            err = "cant write the unpacked demo";
            return false;
        }
        bool ok = basefix::unmute(plain.data(), len, o);
        if (!ok) {
            dbg::print("[launch] %s: no mute fields to rename", f.name.c_str());
            ok = fseek(o, 0, SEEK_SET) == 0 && fwrite(plain.data(), 1, len, o) == len;
        }
        fclose(o);
        if (!ok || !MoveFileExW(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp.c_str());
            err = "cant write the unpacked demo";
            return false;
        }
        std::vector<std::string> index = read_index();
        index.push_back(name + "\t" + f.path + "\t" + std::to_string((long long)time(nullptr)));
        write_index(index);
        return true;
    }

    bool repair(const demo_file_t& f, const std::wstring& dst, std::string& path, std::string& err) {
        bool unpacked = path != f.path;
        if (!unpacked)
            trim(KEEP - 1);
        std::string out = text::utf8(dst);
        uint64_t t0 = GetTickCount64();
        if (!basefix::fix(path, out, err))
            return false;
        std::vector<std::string> index = read_index();
        if (unpacked) {
            DeleteFileW(text::wide(path).c_str());
            std::string plain = path.substr(path.find_last_of("\\/") + 1) + "\t";
            index.erase(std::remove_if(index.begin(), index.end(), [&](const std::string& l) { return l.rfind(plain, 0) == 0; }), index.end());
        }
        index.push_back(out.substr(out.find_last_of("\\/") + 1) + "\t" + f.path + "\t" + std::to_string((long long)time(nullptr)));
        write_index(index);
        dbg::print("[launch] fixed %s in %llu ms", f.name.c_str(), (unsigned long long)(GetTickCount64() - t0));
        path = out;
        return true;
    }

    void fail(const job_t& j, const std::string& err) {
        dbg::print("[launch] %s: %s", j.file.name.c_str(), err.c_str());
        if (j.copy)
            post("error", ",\"err\":\"" + json::escape(err) + "\"");
        else
            window::post_json("{\"type\":\"fixed\",\"path\":\"" + json::escape(j.file.path) + "\",\"ok\":false,\"err\":\"" + json::escape(err) + "\"}");
    }

    void run(const job_t& j) {
        std::string path = j.file.path, err;
        std::string lower = text::lower(j.file.name);
        bool packed = lower.size() > 8 && lower.compare(lower.size() - 8, 8, ".dem.zst") == 0;
        std::wstring fixed = g_dir + L"\\" + text::wide(j.file.name.substr(0, lower.rfind(".dem")) + "-fixed.dem");
        if (j.fix && GetFileAttributesW(fixed.c_str()) != INVALID_FILE_ATTRIBUTES) {
            path = text::utf8(fixed);
            touch(path.substr(path.find_last_of("\\/") + 1));
        } else {
            if (j.copy && (packed || j.fix))
                post("unpacking", "");
            uint64_t t0 = GetTickCount64();
            if (packed) {
                if (!unpack(j.file, path, err))
                    return fail(j, err);
                dbg::print("[launch] unpacked %s in %llu ms", j.file.name.c_str(), (unsigned long long)(GetTickCount64() - t0));
            }
            if (j.fix && !repair(j.file, fixed, path, err))
                return fail(j, err);
        }
        if (!j.copy) {
            window::post_json("{\"type\":\"fixed\",\"path\":\"" + json::escape(j.file.path) + "\",\"ok\":true,\"err\":\"\"}");
            return;
        }
        std::replace(path.begin(), path.end(), '\\', '/');
        std::string line = "playdemo \"" + path + "\"";
        if (j.low || j.high) {
            std::string voice = "tv_listen_voice_indices " + std::to_string((int32_t)j.low);
            if (j.high)
                voice += "; tv_listen_voice_indices_h " + std::to_string((int32_t)j.high);
            line = voice + "; " + line;
        }
        files::clip(nullptr, line);
        post("copied", "");
    }

    void work() {
        while (true) {
            job_t j;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || g_pending || g_clear; });
                if (g_quit)
                    break;
                if (g_clear) {
                    g_clear = false;
                    lock.unlock();
                    trim(0);
                    window::post_json("{\"type\":\"cleared\"}");
                    continue;
                }
                j = g_job;
                g_pending = false;
            }
            run(j);
        }
    }

    bool init(const std::wstring& dir) {
        g_dir = dir;
        CreateDirectoryW(dir.c_str(), nullptr);
        g_worker = std::thread(work);
        return true;
    }

    void cleanup() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_quit = true;
        }
        g_cv.notify_all();
        if (g_worker.joinable())
            g_worker.join();
    }

    void clear() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_clear = true;
        }
        g_cv.notify_all();
    }

    void watch(const demo_file_t& f, uint32_t low, uint32_t high, bool fix) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_job = job_t{f, low, high, fix, true};
            g_pending = true;
        }
        g_cv.notify_all();
    }

    std::vector<std::string> fixed_sources() {
        std::vector<std::string> out;
        for (const std::string& l : read_index()) {
            size_t a = l.find('\t'), b = l.rfind('\t');
            std::string name = l.substr(0, a);
            if (a == b || name.size() < 10 || name.compare(name.size() - 10, 10, "-fixed.dem") != 0)
                continue;
            if (GetFileAttributesW((g_dir + L"\\" + text::wide(name)).c_str()) != INVALID_FILE_ATTRIBUTES)
                out.push_back(l.substr(a + 1, b - a - 1));
        }
        return out;
    }

    void prepare_fix(const demo_file_t& f) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_job = job_t{f, 0, 0, true, false};
            g_pending = true;
        }
        g_cv.notify_all();
    }
}
