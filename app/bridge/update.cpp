#include "pch.h"

#include "bridge/update.h"
#include "dbg.h"
#include "json/json.h"
#include "platform/net.h"
#include "platform/setup.h"
#include "platform/window.h"
#include "text.h"

namespace update {
    const wchar_t* LATEST = L"https://api.github.com/repos/krisoft20/cs2voice/releases/latest";
    const wchar_t* API_HEADERS = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28";
    const char* ASSET = "cs2voice.exe";

    std::mutex g_mu;
    std::thread g_check;
    std::thread g_install;
    std::string g_version;
    std::string g_url;
    std::string g_sha;
    std::string g_sha_url;
    bool g_busy = false;

    std::wstring exe_path() {
        wchar_t buf[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::wstring(buf, n);
    }

    std::vector<int> parts(const std::string& v) {
        std::vector<int> out;
        size_t at = v.find_first_of("0123456789");
        while (at != std::string::npos && at < v.size()) {
            out.push_back(atoi(v.c_str() + at));
            at = v.find('.', at);
            if (at != std::string::npos)
                at++;
        }
        out.resize(3);
        return out;
    }

    void tidy() {
        DeleteFileW((exe_path() + L".old").c_str());
    }

    void post_state(const std::string& state, const std::string& err = "") {
        window::post_json("{\"type\":\"update_state\",\"state\":\"" + state + "\",\"err\":\"" + json::escape(err) + "\"}");
    }

    void run_check() {
        std::string body;
        json_t r;
        std::string err;
        if (!net::get(LATEST, body, API_HEADERS) || !json::parse(body, r, err)) {
            dbg::print("[update] no release info");
            return;
        }
        std::string tag = json::str(json::at(r, "tag_name"));
        if (tag.empty() || parts(tag) <= parts(CS2VOICE_VERSION)) {
            dbg::print("[update] %s is the newest", CS2VOICE_VERSION);
            return;
        }
        std::string url, sha, sha_url;
        double size = 0;
        if (const json_t* assets = json::at(r, "assets"); assets && assets->kind == jk_arr) {
            for (const json_t& a : assets->arr) {
                std::string name = json::str(json::at(a, "name"));
                if (name == ASSET) {
                    url = json::str(json::at(a, "browser_download_url"));
                    size = json::num(json::at(a, "size"));
                    std::string digest = json::str(json::at(a, "digest"));
                    if (digest.rfind("sha256:", 0) == 0)
                        sha = digest.substr(7);
                } else if (name == std::string(ASSET) + ".sha256") {
                    sha_url = json::str(json::at(a, "browser_download_url"));
                }
            }
        }
        if (url.empty()) {
            dbg::print("[update] %s has no %s", tag.c_str(), ASSET);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_version = tag;
            g_url = url;
            g_sha = sha;
            g_sha_url = sha_url;
        }
        dbg::print("[update] %s is out", tag.c_str());
        window::post_json("{\"type\":\"update\",\"version\":\"" + json::escape(tag) + "\",\"notes\":\"" + json::escape(json::str(json::at(r, "body"))) +
                          "\",\"size\":" + std::to_string((uint64_t)size) + "}");
    }

    void check() {
        if (setup::packaged()) {
            dbg::print("[update] store package, the store updates it");
            return;
        }
        if (g_check.joinable())
            return;
        g_check = std::thread(run_check);
    }

    void on_progress(uint64_t done, uint64_t total, void*) {
        static uint64_t last = 0;
        if (done - last < (1 << 18) && done != total)
            return;
        last = done;
        window::post_json("{\"type\":\"update_state\",\"state\":\"downloading\",\"done\":" + std::to_string(done) + ",\"total\":" +
                          std::to_string(total) + "}");
    }

    void run_install() {
        std::string url, sha, sha_url;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            url = g_url;
            sha = g_sha;
            sha_url = g_sha_url;
        }
        if (sha.empty() && !sha_url.empty()) {
            std::string text;
            if (net::get(text::wide(sha_url), text))
                sha = text.substr(0, text.find_first_not_of("0123456789abcdefABCDEF"));
        }
        if (sha.size() != 64) {
            post_state("failed", "the release has no checksum");
            return;
        }
        std::string exe;
        if (!net::get(text::wide(url), exe, nullptr, on_progress)) {
            post_state("failed", "download failed");
            return;
        }
        if (_stricmp(net::sha256(exe).c_str(), sha.c_str()) != 0) {
            post_state("failed", "checksum does not match");
            return;
        }
        std::wstring path = exe_path(), fresh = path + L".new", old = path + L".old";
        FILE* f = _wfopen(fresh.c_str(), L"wb");
        bool ok = f && fwrite(exe.data(), 1, exe.size(), f) == exe.size();
        if (f)
            fclose(f);
        if (!ok || !MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(fresh.c_str());
            post_state("failed", "cant replace the exe");
            return;
        }
        if (!MoveFileExW(fresh.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            MoveFileExW(old.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
            post_state("failed", "cant replace the exe");
            return;
        }
        std::wstring cmd = L"\"" + path + L"\" --after-update " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(path.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            post_state("failed", "cant start the new version");
            return;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        dbg::print("[update] installed, restarting");
        window::command("quit");
    }

    void install() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_busy || g_url.empty())
                return;
            g_busy = true;
        }
        if (g_install.joinable())
            g_install.join();
        g_install = std::thread([] {
            run_install();
            std::lock_guard<std::mutex> lock(g_mu);
            g_busy = false;
        });
    }

    void cleanup() {
        if (g_check.joinable())
            g_check.join();
        if (g_install.joinable())
            g_install.join();
    }
}
