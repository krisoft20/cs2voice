#include "pch.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincrypt.h>

#include "bridge/bridge.h"
#include "bridge/inventory.h"
#include "bridge/jobs.h"
#include "bridge/launch.h"
#include "bridge/maps.h"
#include "bridge/perfs.h"
#include "bridge/profiles.h"
#include "bridge/tracks.h"
#include "bridge/update.h"
#include "bridge/voices.h"
#include "bridge/watch.h"
#include "dbg.h"
#include "demos/finder.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/setup.h"
#include "platform/window.h"
#include "text.h"
#include "web/web.h"

namespace bridge {
    std::wstring g_settings;
    std::wstring g_data;
    std::vector<std::string> g_defaults;
    std::vector<std::string> g_custom;
    std::vector<demo_file_t> g_files;
    std::string g_lang = "system";
    std::string g_loader = "radar";
    std::wstring g_opened_path;
    std::map<std::string, int64_t> g_opened;
    std::set<std::string> g_keep;
    std::set<std::string> g_skip;
    std::string g_cleanup = "ask";
    std::string g_cleanup_method = "recycle";
    int g_cleanup_days = 7;
    bool g_cleanup_copies = true;
    bool g_cleanup_broken = true;
    bool g_cleanup_mute = false;
    int64_t g_snooze = 0;
    bool g_updates = true;
    std::string g_close = "ask";

    std::vector<std::string> all_folders() {
        std::vector<std::string> out = g_defaults;
        for (const std::string& c : g_custom)
            if (std::find(out.begin(), out.end(), c) == out.end())
                out.push_back(c);
        return out;
    }

    void load_settings() {
        FILE* f = _wfopen(g_settings.c_str(), L"rb");
        if (!f)
            return;
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            files::chomp(s);
            if (s.rfind("folder=", 0) == 0)
                g_custom.push_back(s.substr(7));
            else if (s.rfind("lang=", 0) == 0)
                g_lang = s.substr(5);
            else if (s.rfind("loader=", 0) == 0)
                g_loader = s.substr(7);
            else if (s.rfind("keep=", 0) == 0)
                g_keep.insert(s.substr(5));
            else if (s.rfind("cleanup_skip=", 0) == 0)
                g_skip.insert(s.substr(13));
            else if (s.rfind("cleanup=", 0) == 0)
                g_cleanup = s.substr(8);
            else if (s.rfind("cleanup_method=", 0) == 0)
                g_cleanup_method = s.substr(15);
            else if (s.rfind("cleanup_days=", 0) == 0)
                g_cleanup_days = std::clamp(atoi(s.c_str() + 13), 1, 365);
            else if (s.rfind("cleanup_copies=", 0) == 0)
                g_cleanup_copies = s.substr(15) == "1";
            else if (s.rfind("cleanup_broken=", 0) == 0)
                g_cleanup_broken = s.substr(15) == "1";
            else if (s.rfind("cleanup_mute=", 0) == 0)
                g_cleanup_mute = s.substr(13) == "1";
            else if (s.rfind("snooze=", 0) == 0)
                g_snooze = _atoi64(s.c_str() + 7);
            else if (s.rfind("updates=", 0) == 0)
                g_updates = s.substr(8) == "1";
            else if (s.rfind("close=", 0) == 0)
                g_close = s.substr(6);
        }
        fclose(f);
    }

    void load_opened() {
        FILE* f = _wfopen(g_opened_path.c_str(), L"rb");
        if (!f)
            return;
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            files::chomp(s);
            size_t tab = s.find('\t');
            if (tab != std::string::npos)
                g_opened[s.substr(tab + 1)] = _atoi64(s.substr(0, tab).c_str());
        }
        fclose(f);
    }

    void save_opened() {
        FILE* f = _wfopen(g_opened_path.c_str(), L"wb");
        if (!f)
            return;
        for (const auto& [path, t] : g_opened)
            fprintf(f, "%lld\t%s\n", (long long)t, path.c_str());
        fclose(f);
    }

    void save_settings() {
        FILE* f = _wfopen(g_settings.c_str(), L"wb");
        if (!f)
            return;
        fprintf(f, "lang=%s\n", g_lang.c_str());
        fprintf(f, "loader=%s\n", g_loader.c_str());
        fprintf(f, "updates=%d\n", g_updates ? 1 : 0);
        fprintf(f, "close=%s\n", g_close.c_str());
        for (const std::string& c : g_custom)
            fprintf(f, "folder=%s\n", c.c_str());
        fprintf(f, "cleanup=%s\ncleanup_method=%s\ncleanup_days=%d\ncleanup_copies=%d\ncleanup_broken=%d\ncleanup_mute=%d\nsnooze=%lld\n", g_cleanup.c_str(),
                g_cleanup_method.c_str(), g_cleanup_days, g_cleanup_copies ? 1 : 0, g_cleanup_broken ? 1 : 0, g_cleanup_mute ? 1 : 0, (long long)g_snooze);
        for (const std::string& k : g_keep)
            fprintf(f, "keep=%s\n", k.c_str());
        for (const std::string& k : g_skip)
            fprintf(f, "cleanup_skip=%s\n", k.c_str());
        fclose(f);
    }

    void send_settings() {
        std::string skip;
        for (const std::string& k : g_skip)
            skip += (skip.empty() ? "\"" : ",\"") + json::escape(k) + "\"";
        setup_t st = setup::state();
        std::string inst = std::string("{\"installed\":") + (st.installed ? "true" : "false") + ",\"here\":" + (st.here ? "true" : "false") +
                           ",\"desktop\":" + (st.desktop ? "true" : "false") + ",\"autostart\":" + (st.autostart ? "true" : "false") + ",\"dir\":\"" +
                           json::escape(text::utf8(st.dir)) + "\"}";
        web::post("{\"type\":\"settings\",\"version\":\"" CS2VOICE_VERSION "\",\"setup\":" + inst + ",\"lang\":\"" + json::escape(g_lang) + "\",\"loader\":\"" +
                  json::escape(g_loader) + "\",\"updates\":" + (g_updates ? "true" : "false") + ",\"close\":\"" + json::escape(g_close) + "\",\"cleanup\":{\"mode\":\"" + json::escape(g_cleanup) + "\",\"method\":\"" + json::escape(g_cleanup_method) +
                  "\",\"days\":" + std::to_string(g_cleanup_days) + ",\"copies\":" + (g_cleanup_copies ? "true" : "false") +
                  ",\"broken\":" + (g_cleanup_broken ? "true" : "false") + ",\"mute\":" + (g_cleanup_mute ? "true" : "false") +
                  ",\"snooze\":" + std::to_string(g_snooze) + ",\"skip\":[" + skip + "]}}");
    }

    void send_folders() {
        std::string j = "{\"type\":\"folders\",\"items\":[";
        std::vector<std::string> all = all_folders();
        for (size_t i = 0; i < all.size(); i++) {
            bool custom = std::find(g_custom.begin(), g_custom.end(), all[i]) != g_custom.end() &&
                          std::find(g_defaults.begin(), g_defaults.end(), all[i]) == g_defaults.end();
            if (i)
                j += ",";
            j += "{\"path\":\"" + json::escape(all[i]) + "\",\"custom\":" + (custom ? "true" : "false") + "}";
        }
        j += "]}";
        web::post(j);
    }

    void restart_watch() {
        watch::cleanup();
        watch::init(all_folders());
    }

    void rescan() {
        finder::scan(all_folders(), g_files);
        dbg::print("[bridge] %zu demos found", g_files.size());
        std::string j = "{\"type\":\"demos\",\"items\":[";
        for (size_t i = 0; i < g_files.size(); i++) {
            const demo_file_t& f = g_files[i];
            if (i)
                j += ",";
            j += "{\"path\":\"" + json::escape(f.path) + "\",\"name\":\"" + json::escape(f.name) + "\",\"folder\":\"" +
                 json::escape(f.folder) + "\",\"size\":" + std::to_string(f.size) + ",\"mtime\":" + std::to_string(f.mtime);
            auto op = g_opened.find(f.path);
            if (op != g_opened.end())
                j += ",\"opened\":" + std::to_string(op->second);
            if (g_keep.count(f.path))
                j += ",\"keep\":true";
            std::string info;
            bool stale = false;
            if (jobs::cached(f, info, stale))
                j += ",\"info\":" + info;
            j += "}";
        }
        j += "]}";
        web::post(j);
        std::vector<std::string> ids;
        bool all = true;
        for (const demo_file_t& f : g_files) {
            std::string info, err;
            json_t r;
            bool stale = false;
            bool full = false;
            bool have = jobs::cached(f, info, stale, &full);
            if (!have || (!full && info.find("\"voice_known\":true") == std::string::npos)) {
                jobs::want(f, false, true);
                all = false;
            }
            if (!have || !full || stale)
                jobs::want(f, false, false);
            perfs::queue(f);
            if (have && json::parse(info, r, err)) {
                if (const json_t* ps = json::at(r, "players"); ps && ps->kind == jk_arr)
                    for (const json_t& p : ps->arr)
                        ids.push_back(json::str(json::at(p, "steamid")));
            }
        }
        if (all && !g_files.empty())
            profiles::keep(ids);
        profiles::want(ids, false);
    }

    const demo_file_t* file_of(const std::string& path) {
        for (const demo_file_t& f : g_files)
            if (f.path == path)
                return &f;
        return nullptr;
    }

    void open(const std::string& path) {
        const demo_file_t* f = file_of(path);
        if (!f)
            return;
        g_opened[path] = (int64_t)time(nullptr);
        save_opened();
        std::string info, map;
        bool stale = false;
        bool full = false;
        bool have = jobs::cached(*f, info, stale, &full);
        json_t r;
        std::string err;
        if (have && json::parse(info, r, err))
            map = json::str(json::at(r, "map"));
        if (have)
            web::post(info);
        else
            web::post("{\"type\":\"reading\",\"path\":\"" + json::escape(path) + "\"}");
        if (!have || stale || !full)
            jobs::want(*f, true);
        voices::want(*f, false);
        perfs::want(*f, map);
        tracks::want(*f, false);
    }

    void copy(const std::string& s) {
        files::clip(window::handle(), s);
        web::post("{\"type\":\"copied\",\"text\":\"" + json::escape(s) + "\"}");
    }

    std::string show(IFileDialog* dlg) {
        std::string out;
        if (SUCCEEDED(dlg->Show(window::handle()))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    out = text::utf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
        return out;
    }

    std::string pick_folder() {
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            return {};
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        return show(dlg);
    }

    std::string pick_save(const std::string& name) {
        IFileSaveDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))))
            return {};
        COMDLG_FILTERSPEC types[] = {{L"WAV", L"*.wav"}};
        dlg->SetFileTypes(1, types);
        dlg->SetDefaultExtension(L"wav");
        std::wstring w = text::wide(name);
        for (wchar_t& c : w)
            if (wcschr(L"\\/:*?\"<>|", c))
                c = L'_';
        dlg->SetFileName(w.c_str());
        return show(dlg);
    }

    void save_clip(const std::string& name, const std::string& b64) {
        DWORD n = 0;
        if (!CryptStringToBinaryA(b64.data(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &n, nullptr, nullptr) || !n) {
            dbg::print("[bridge] clip is not base64");
            return;
        }
        std::string bytes(n, '\0');
        CryptStringToBinaryA(b64.data(), (DWORD)b64.size(), CRYPT_STRING_BASE64, (BYTE*)bytes.data(), &n, nullptr, nullptr);
        std::string path = pick_save(name);
        if (path.empty())
            return;
        FILE* f = _wfopen(text::wide(path).c_str(), L"wb");
        if (!f) {
            dbg::print("[bridge] cant write %s", path.c_str());
            return;
        }
        bool ok = fwrite(bytes.data(), 1, n, f) == n;
        fclose(f);
        dbg::print("[bridge] clip %s, %lu bytes%s", path.c_str(), n, ok ? "" : ", write failed");
        if (ok)
            web::post("{\"type\":\"saved\",\"path\":\"" + json::escape(path) + "\"}");
    }

    uint64_t dir_size(const std::wstring& dir, int& files) {
        uint64_t total = 0;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return 0;
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                total += dir_size(dir + L"\\" + fd.cFileName, files);
            } else {
                total += ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                files++;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return total;
    }

    uint64_t file_size(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a))
            return 0;
        return ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }

    void send_storage() {
        const wchar_t* parts[][2] = {{L"play", L"unpacked"}, {L"avatars", L"avatars"}, {L"maps", L"maps"}, {L"webview", L"webview"}};
        uint64_t results = file_size(g_data + L"\\cache.txt") + file_size(g_data + L"\\profiles.txt") + file_size(g_data + L"\\perfs.txt");
        int stored = 3;
        results += dir_size(g_data + L"\\tracks", stored);
        std::string j = "{\"type\":\"storage\",\"items\":[{\"key\":\"results\",\"bytes\":" + std::to_string(results) + ",\"files\":" + std::to_string(stored) + "},";
        for (size_t i = 0; i < 4; i++) {
            int files = 0;
            uint64_t bytes = dir_size(g_data + L"\\" + parts[i][0], files);
            if (i)
                j += ",";
            j += "{\"key\":\"" + text::utf8(parts[i][1]) + "\",\"bytes\":" + std::to_string(bytes) + ",\"files\":" + std::to_string(files) + "}";
        }
        j += "]}";
        web::post(j);
    }

    void add_folder() {
        std::string f = pick_folder();
        if (f.empty() || std::find(g_custom.begin(), g_custom.end(), f) != g_custom.end())
            return;
        g_custom.push_back(f);
        save_settings();
        restart_watch();
        send_folders();
        rescan();
    }

    void remove_folder(const std::string& f) {
        auto it = std::find(g_custom.begin(), g_custom.end(), f);
        if (it == g_custom.end())
            return;
        g_custom.erase(it);
        save_settings();
        restart_watch();
        send_folders();
        rescan();
    }

    void open_profile(const std::string& id) {
        if (id.empty() || id.size() > 20 || id.find_first_not_of("0123456789") != std::string::npos)
            return;
        std::wstring url = L"https://steamcommunity.com/profiles/" + text::wide(id);
        ShellExecuteW(window::handle(), L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    std::vector<std::string> strings(const json_t* list) {
        std::vector<std::string> out;
        if (list && list->kind == jk_arr)
            for (const json_t& v : list->arr)
                out.push_back(json::str(&v));
        return out;
    }

    std::vector<std::string> known_paths(const json_t* list) {
        std::vector<std::string> out;
        for (const std::string& p : strings(list))
            if (file_of(p))
                out.push_back(p);
        return out;
    }

    bool file_op(UINT op, const std::vector<std::string>& paths, const std::string& to, bool undo = true) {
        if (paths.empty())
            return false;
        std::wstring from;
        for (const std::string& p : paths) {
            from += text::wide(p);
            from.push_back(L'\0');
        }
        from.push_back(L'\0');
        std::wstring dest = text::wide(to);
        dest.push_back(L'\0');
        SHFILEOPSTRUCTW fo = {};
        fo.hwnd = window::handle();
        fo.wFunc = op;
        fo.pFrom = from.c_str();
        fo.pTo = to.empty() ? nullptr : dest.c_str();
        fo.fFlags = (undo ? FOF_ALLOWUNDO | FOF_WANTNUKEWARNING : 0) | (op == FO_DELETE ? FOF_NOCONFIRMATION : 0);
        int r = SHFileOperationW(&fo);
        dbg::print("[bridge] %s %zu files%s%s: %d%s", op == FO_DELETE ? (undo ? "recycled" : "deleted") : "moved", paths.size(), to.empty() ? "" : " to ",
                   to.c_str(), r, fo.fAnyOperationsAborted ? " (aborted)" : "");
        return r == 0 && !fo.fAnyOperationsAborted;
    }

    void forget(const std::vector<std::string>& paths) {
        jobs::forget(paths);
        perfs::forget(paths);
        tracks::forget(paths);
        bool changed = false;
        for (const std::string& p : paths) {
            changed |= g_opened.erase(p) > 0;
            changed |= g_keep.erase(p) > 0;
        }
        if (changed) {
            save_opened();
            save_settings();
        }
    }

    void delete_demos(const std::vector<std::string>& paths, bool undo = true) {
        std::vector<std::string> files = paths;
        for (const std::string& p : paths) {
            std::string info = p + ".info";
            if (GetFileAttributesW(text::wide(info).c_str()) != INVALID_FILE_ATTRIBUTES)
                files.push_back(info);
        }
        file_op(FO_DELETE, files, {}, undo);
        std::vector<std::string> gone;
        for (const std::string& p : paths)
            if (GetFileAttributesW(text::wide(p).c_str()) == INVALID_FILE_ATTRIBUTES)
                gone.push_back(p);
        forget(gone);
        rescan();
    }

    void move_demos(const std::vector<std::string>& paths) {
        if (paths.empty())
            return;
        std::string to = pick_folder();
        if (to.empty())
            return;
        file_op(FO_MOVE, paths, to);
        rescan();
    }

    void open_url(const std::string& url) {
        const char* ALLOWED = "https://github.com/krisoft20/cs2voice";
        if (url.rfind(ALLOWED, 0) != 0)
            return;
        ShellExecuteW(window::handle(), L"open", text::wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void reveal(const std::string& path) {
        std::wstring args = L"/select,\"" + text::wide(path) + L"\"";
        ShellExecuteW(window::handle(), L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }

    bool init(const std::wstring& data_dir) {
        g_data = data_dir;
        g_settings = data_dir + L"\\settings.txt";
        g_opened_path = data_dir + L"\\opened.txt";
        load_settings();
        if (g_close != "tray" && g_close != "quit")
            g_close = "ask";
        window::set_close(g_close);
        load_opened();
        update::tidy();
        setup::refresh();
        finder::default_folders(g_defaults);
        for (const std::string& f : g_defaults)
            dbg::print("[bridge] folder %s", f.c_str());
        profiles::init(data_dir + L"\\profiles.txt", data_dir + L"\\avatars");
        if (!jobs::init(data_dir + L"\\cache.txt"))
            return false;
        maps::init(data_dir + L"\\maps");
        inventory::init(data_dir + L"\\maps\\econ");
        voices::init();
        tracks::init(data_dir + L"\\tracks");
        perfs::init(data_dir + L"\\perfs.txt");
        launch::init(data_dir + L"\\play");
        return watch::init(all_folders());
    }

    void cleanup() {
        update::cleanup();
        watch::cleanup();
        inventory::cleanup();
        voices::cleanup();
        tracks::cleanup();
        perfs::cleanup();
        launch::cleanup();
        jobs::cleanup();
        profiles::cleanup();
    }

    void handle(const std::string& msg) {
        json_t j;
        std::string err;
        if (!json::parse(msg, j, err)) {
            dbg::print("[bridge] bad message: %s", err.c_str());
            return;
        }
        std::string type = json::str(json::at(j, "type"));
        if (type == "ready") {
            dbg::print("[bridge] ui ready");
            send_settings();
            if (g_updates)
                update::check();
            web::post("{\"type\":\"me\",\"steamid\":\"" + std::to_string(finder::current_user()) + "\"}");
            send_folders();
            rescan();
        } else if (type == "open") {
            open(json::str(json::at(j, "path")));
        } else if (type == "copy") {
            copy(json::str(json::at(j, "text")));
        } else if (type == "rescan") {
            rescan();
        } else if (type == "add_folder") {
            add_folder();
        } else if (type == "remove_folder") {
            remove_folder(json::str(json::at(j, "path")));
        } else if (type == "profiles") {
            profiles::want(strings(json::at(j, "ids")), true);
        } else if (type == "set") {
            std::string key = json::str(json::at(j, "key")), value = json::str(json::at(j, "value"));
            if (key == "lang" && !value.empty() && value.size() <= 8 && value.find_first_not_of("abcdefghijklmnopqrstuvwxyz") == std::string::npos) {
                g_lang = value;
                save_settings();
            } else if (key == "loader" && (value == "plant" || value == "radar" || value == "rounds" || value == "ammo")) {
                g_loader = value;
                save_settings();
            } else if (key == "cleanup" && (value == "off" || value == "ask" || value == "auto")) {
                g_cleanup = value;
            } else if (key == "cleanup_method" && (value == "recycle" || value == "delete")) {
                g_cleanup_method = value;
            } else if (key == "cleanup_days") {
                g_cleanup_days = std::clamp(atoi(value.c_str()), 1, 365);
            } else if (key == "cleanup_copies") {
                g_cleanup_copies = value == "1";
            } else if (key == "cleanup_broken") {
                g_cleanup_broken = value == "1";
            } else if (key == "cleanup_mute") {
                g_cleanup_mute = value == "1";
            } else if (key == "snooze") {
                g_snooze = _atoi64(value.c_str());
            } else if (key == "close" && (value == "ask" || value == "tray" || value == "quit")) {
                g_close = value;
                window::set_close(value);
            } else if (key == "autostart" || key == "desktop") {
                if (!setup::state().installed)
                    return;
                if (key == "autostart")
                    setup::set_autostart(value == "1");
                else
                    setup::set_desktop(value == "1");
                send_settings();
                return;
            } else if (key == "updates") {
                g_updates = value == "1";
                if (g_updates)
                    update::check();
            } else {
                return;
            }
            if (key.rfind("cleanup", 0) == 0 || key == "snooze" || key == "updates" || key == "close") {
                save_settings();
                send_settings();
            }
        } else if (type == "cleanup_folder") {
            std::string path = json::str(json::at(j, "path"));
            const json_t* flag = json::at(j, "on");
            bool on = flag && flag->b;
            if (on)
                g_skip.erase(path);
            else
                g_skip.insert(path);
            save_settings();
            send_settings();
        } else if (type == "keep") {
            const json_t* flag = json::at(j, "on");
            bool on = flag && flag->b;
            for (const std::string& p : known_paths(json::at(j, "paths"))) {
                if (on)
                    g_keep.insert(p);
                else
                    g_keep.erase(p);
            }
            save_settings();
            rescan();
        } else if (type == "cleanup") {
            std::string method = json::str(json::at(j, "method"), g_cleanup_method);
            delete_demos(known_paths(json::at(j, "paths")), method != "delete");
        } else if (type == "open_url") {
            open_url(json::str(json::at(j, "url")));
        } else if (type == "install") {
            std::string why;
            const json_t* desk = json::at(j, "desktop");
            const json_t* boot = json::at(j, "autostart");
            if (!setup::install(desk && desk->b, why)) {
                web::post("{\"type\":\"setup_error\",\"err\":\"" + json::escape(why) + "\"}");
                return;
            }
            setup::set_autostart(boot && boot->b);
            if (!setup::state().here && setup::start_installed()) {
                window::command("quit");
            } else {
                send_settings();
            }
        } else if (type == "uninstall") {
            const json_t* wipe = json::at(j, "data");
            setup::uninstall(wipe && wipe->b, g_data);
            window::command("quit");
        } else if (type == "tray_text") {
            window::set_menu(text::wide(json::str(json::at(j, "open"))), text::wide(json::str(json::at(j, "quit"))));
        } else if (type == "win") {
            window::command(json::str(json::at(j, "action")));
        } else if (type == "storage") {
            send_storage();
        } else if (type == "clear_unpacked") {
            launch::clear();
        } else if (type == "clear_cache") {
            jobs::clear();
            perfs::clear();
            tracks::clear();
            profiles::clear();
            maps::clear();
            inventory::clear();
            launch::clear();
            web::post("{\"type\":\"cache_cleared\"}");
            rescan();
            send_storage();
        } else if (type == "watch") {
            if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
                launch::watch(*f, (uint32_t)(int64_t)json::num(json::at(j, "low")), (uint32_t)(int64_t)json::num(json::at(j, "high")));
        } else if (type == "save_clip") {
            save_clip(json::str(json::at(j, "name")), json::str(json::at(j, "data")));
        } else if (type == "track") {
            if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
                tracks::want(*f, true);
        } else if (type == "radar") {
            maps::radar(json::str(json::at(j, "map")));
        } else if (type == "icons") {
            inventory::want_icons();
        } else if (type == "econ_want") {
            inventory::icons(strings(json::at(j, "icons")));
        } else if (type == "skins") {
            if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
                inventory::want(*f, json::str(json::at(j, "lang"), "en"));
        } else if (type == "voice") {
            if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
                voices::want(*f, true);
        } else if (type == "art") {
            maps::ensure(json::str(json::at(j, "map")));
        } else if (type == "steam") {
            open_profile(json::str(json::at(j, "id")));
        } else if (type == "delete_demos") {
            delete_demos(known_paths(json::at(j, "paths")));
        } else if (type == "move_demos") {
            move_demos(known_paths(json::at(j, "paths")));
        } else if (type == "copy_paths") {
            std::string all;
            for (const std::string& p : known_paths(json::at(j, "paths")))
                all += (all.empty() ? "" : "\r\n") + p;
            if (!all.empty())
                copy(all);
        } else if (type == "update_install") {
            update::install();
        } else if (type == "reveal") {
            reveal(json::str(json::at(j, "path")));
        } else {
            dbg::print("[bridge] unknown message %s", type.c_str());
        }
    }
}
