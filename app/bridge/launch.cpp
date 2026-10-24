#include "pch.h"

#include "bridge/launch.h"
#include "dbg.h"
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
    };

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

    void clear_dir() {
        std::vector<std::string> kept;
        for (const std::string& l : read_index()) {
            std::string file = l.substr(0, l.find('\t'));
            std::wstring path = g_dir + L"\\" + text::wide(file);
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && !DeleteFileW(path.c_str()))
                kept.push_back(l);
        }
        files::wipe(g_dir, L"*.dem*");
        write_index(kept);
        if (!kept.empty())
            dbg::print("[launch] %zu unpacked demos are still in use, will retry", kept.size());
    }

    bool unpack(const demo_file_t& f, std::string& out, std::string& err) {
        std::string name = f.name.substr(0, f.name.size() - 4);
        std::wstring dst = g_dir + L"\\" + text::wide(name);
        out = text::utf8(dst);
        if (GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES)
            return true;
        std::vector<uint8_t> packed;
        zstdx::buffer_t plain;
        if (!read_all(text::wide(f.path), packed)) {
            err = "cant read the demo";
            return false;
        }
        size_t len = 0;
        if (!zstdx::decompress(packed.data(), packed.size(), plain, len, err))
            return false;
        clear_dir();
        std::wstring tmp = dst + L".part";
        FILE* o = _wfopen(tmp.c_str(), L"wb");
        if (!o) {
            err = "cant write the unpacked demo";
            return false;
        }
        bool ok = fwrite(plain.data(), 1, len, o) == len;
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

    void run(const job_t& j) {
        std::string path = j.file.path, err;
        std::string lower = text::lower(j.file.name);
        if (lower.size() > 8 && lower.compare(lower.size() - 8, 8, ".dem.zst") == 0) {
            post("unpacking", "");
            uint64_t t0 = GetTickCount64();
            if (!unpack(j.file, path, err)) {
                dbg::print("[launch] %s: %s", j.file.name.c_str(), err.c_str());
                post("error", ",\"err\":\"" + json::escape(err) + "\"");
                return;
            }
            dbg::print("[launch] unpacked %s in %llu ms", j.file.name.c_str(), (unsigned long long)(GetTickCount64() - t0));
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
                    clear_dir();
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

    void watch(const demo_file_t& f, uint32_t low, uint32_t high) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_job = job_t{f, low, high};
            g_pending = true;
        }
        g_cv.notify_all();
    }
}
