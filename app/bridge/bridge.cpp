#include "pch.h"

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincrypt.h>

#include "bridge/bridge.h"
#include "bridge/faceit.h"
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
    bool g_talk = false;
    int g_talk_style = 0;
    int g_talk_lead = 13;
    int g_talk_min = 32;
    int g_talk_refresh = 192;
    std::string g_close = "ask";
    bool g_steam = true;

    std::vector<std::string> all_folders() {
        std::vector<std::string> out = g_defaults;
        for (const std::string& c : g_custom)
            if (std::find(out.begin(), out.end(), c) == out.end())
                out.push_back(c);
        return out;
    }

    enum set_key_t {
        sk_none,
        sk_lang,
        sk_loader,
        sk_cleanup,
        sk_cleanup_method,
        sk_cleanup_days,
        sk_cleanup_copies,
        sk_cleanup_broken,
        sk_cleanup_mute,
        sk_folder,
        sk_keep,
        sk_cleanup_skip,
        sk_snooze,
        sk_close,
        sk_steam,
        sk_autostart,
        sk_desktop,
        sk_updates,
        sk_talk,
        sk_talk_style,
        sk_talk_lead,
        sk_talk_min,
        sk_talk_refresh,
    };

    set_key_t set_key(const std::string& s) {
        static const std::unordered_map<std::string, set_key_t> keys = {
            {"lang", sk_lang},
            {"loader", sk_loader},
            {"cleanup", sk_cleanup},
            {"cleanup_method", sk_cleanup_method},
            {"cleanup_days", sk_cleanup_days},
            {"cleanup_copies", sk_cleanup_copies},
            {"cleanup_broken", sk_cleanup_broken},
            {"cleanup_mute", sk_cleanup_mute},
            {"folder", sk_folder},
            {"keep", sk_keep},
            {"cleanup_skip", sk_cleanup_skip},
            {"snooze", sk_snooze},
            {"close", sk_close},
            {"steam", sk_steam},
            {"autostart", sk_autostart},
            {"desktop", sk_desktop},
            {"updates", sk_updates},
            {"talk", sk_talk},
            {"talk_style", sk_talk_style},
            {"talk_lead", sk_talk_lead},
            {"talk_min", sk_talk_min},
            {"talk_refresh", sk_talk_refresh},
        };
        std::unordered_map<std::string, set_key_t>::const_iterator it = keys.find(s);
        return it == keys.end() ? sk_none : it->second;
    }
    void load_settings() {
        FILE* f = _wfopen(g_settings.c_str(), L"rb");
        if (!f)
            return;
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            files::chomp(s);
            size_t eq = s.find('=');
            if (eq == std::string::npos)
                continue;
            std::string name = s.substr(0, eq), value = s.substr(eq + 1);
            switch (set_key(name)) {
            case sk_folder:
                g_custom.push_back(value);
                break;
            case sk_keep:
                g_keep.insert(value);
                break;
            case sk_cleanup_skip:
                g_skip.insert(value);
                break;
            case sk_lang:
                g_lang = value;
                break;
            case sk_loader:
                g_loader = value;
                break;
            case sk_cleanup:
                g_cleanup = value;
                break;
            case sk_cleanup_method:
                g_cleanup_method = value;
                break;
            case sk_cleanup_days:
                g_cleanup_days = std::clamp(atoi(value.c_str()), 1, 365);
                break;
            case sk_cleanup_copies:
                g_cleanup_copies = value == "1";
                break;
            case sk_cleanup_broken:
                g_cleanup_broken = value == "1";
                break;
            case sk_cleanup_mute:
                g_cleanup_mute = value == "1";
                break;
            case sk_snooze:
                g_snooze = _atoi64(value.c_str());
                break;
            case sk_close:
                g_close = value;
                break;
            case sk_steam:
                g_steam = value == "1";
                break;
            case sk_updates:
                g_updates = value == "1";
                break;
            case sk_talk:
                g_talk = value == "1";
                break;
            case sk_talk_style:
                g_talk_style = std::clamp(atoi(value.c_str()), 0, 1);
                break;
            case sk_talk_lead:
                g_talk_lead = std::clamp(atoi(value.c_str()), 0, 640);
                break;
            case sk_talk_min:
                g_talk_min = std::clamp(atoi(value.c_str()), 0, 640);
                break;
            case sk_talk_refresh:
                g_talk_refresh = std::clamp(atoi(value.c_str()), 32, 1920);
                break;
            default:
                break;
            }
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
        fprintf(f, "talk=%d\n", g_talk ? 1 : 0);
        fprintf(f, "talk_style=%d\n", g_talk_style);
        fprintf(f, "talk_lead=%d\n", g_talk_lead);
        fprintf(f, "talk_min=%d\n", g_talk_min);
        fprintf(f, "talk_refresh=%d\n", g_talk_refresh);
        fprintf(f, "close=%s\n", g_close.c_str());
        fprintf(f, "steam=%d\n", g_steam ? 1 : 0);
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
                           ",\"store\":" + (setup::packaged() ? "true" : "false") + ",\"desktop\":" + (st.desktop ? "true" : "false") + ",\"autostart\":" + (st.autostart ? "true" : "false") + ",\"dir\":\"" +
                           json::escape(text::utf8(st.dir)) + "\"}";
        web::post("{\"type\":\"settings\",\"version\":\"" CS2VOICE_VERSION "\",\"setup\":" + inst + ",\"lang\":\"" + json::escape(g_lang) + "\",\"loader\":\"" +
                  json::escape(g_loader) + "\",\"updates\":" + (g_updates ? "true" : "false") + ",\"talk\":" + (g_talk ? "true" : "false") + ",\"talkStyle\":" + std::to_string(g_talk_style) + ",\"talkLead\":" + std::to_string(g_talk_lead) + ",\"talkMin\":" + std::to_string(g_talk_min) + ",\"talkRefresh\":" + std::to_string(g_talk_refresh) + ",\"close\":\"" + json::escape(g_close) + "\",\"steam\":" + (g_steam ? "true" : "false") + ",\"cleanup\":{\"mode\":\"" + json::escape(g_cleanup) + "\",\"method\":\"" + json::escape(g_cleanup_method) +
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
        std::string info;
        bool stale = false;
        bool full = false;
        bool have = jobs::cached(*f, info, stale, &full);
        if (have)
            web::post(info);
        else
            web::post("{\"type\":\"reading\",\"path\":\"" + json::escape(path) + "\"}");
        if (!have || stale || !full)
            jobs::want(*f, true);
        voices::want(*f, false);
        perfs::want(*f);
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

    void open_faceit(const std::string& nick) {
        const char* OK = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
        if (nick.empty() || nick.size() > 32 || nick.find_first_not_of(OK) != std::string::npos)
            return;
        std::wstring url = L"https://www.faceit.com/en/players/" + text::wide(nick);
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

    std::string fixed_path(const std::string& path) {
        std::string out = path;
        for (const char* ext : {".dem.zst", ".dem"})
            if (out.size() > strlen(ext) && out.compare(out.size() - strlen(ext), strlen(ext), ext) == 0) {
                out.resize(out.size() - strlen(ext));
                break;
            }
        return out + "-fixed.dem";
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
        launch::set_talk(g_talk, g_talk_style, g_talk_lead, g_talk_min, g_talk_refresh);
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
        profiles::set_online(g_steam);
        faceit::init(data_dir + L"\\faceit.txt");
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
    void cmd_ready(const json_t&) {
        dbg::print("[bridge] ui ready");
        send_settings();
        if (g_updates)
            update::check();
        web::post("{\"type\":\"me\",\"steamid\":\"" + std::to_string(finder::current_user()) + "\"}");
        send_folders();
        rescan();
        for (const std::string& src : launch::fixed_sources())
            web::post("{\"type\":\"fixed\",\"path\":\"" + json::escape(src) + "\",\"ok\":true,\"err\":\"\"}");
    }

    void cmd_open(const json_t& j) {
        open(json::str(json::at(j, "path")));
    }

    void cmd_copy(const json_t& j) {
        copy(json::str(json::at(j, "text")));
    }

    void cmd_rescan(const json_t&) {
        rescan();
    }

    void cmd_add_folder(const json_t&) {
        add_folder();
    }

    void cmd_remove_folder(const json_t& j) {
        remove_folder(json::str(json::at(j, "path")));
    }

    void cmd_faceit(const json_t& j) {
        if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))); f && g_steam)
            faceit::want(f->path, f->name);
    }

    void cmd_profiles(const json_t& j) {
        profiles::want(strings(json::at(j, "ids")), true);
    }

    void cmd_set(const json_t& j) {
        std::string key = json::str(json::at(j, "key")), value = json::str(json::at(j, "value"));
        bool save = false, talking = false;
        switch (set_key(key)) {
        case sk_lang:
            if (value.empty() || value.size() > 8 || value.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos)
                return;
            g_lang = value;
            save = true;
            break;
        case sk_loader:
            if (value != "plant" && value != "radar" && value != "rounds" && value != "ammo")
                return;
            g_loader = value;
            save = true;
            break;
        case sk_cleanup:
            if (value != "off" && value != "ask" && value != "auto")
                return;
            g_cleanup = value;
            save = true;
            break;
        case sk_cleanup_method:
            if (value != "recycle" && value != "delete")
                return;
            g_cleanup_method = value;
            save = true;
            break;
        case sk_cleanup_days:
            g_cleanup_days = std::clamp(atoi(value.c_str()), 1, 365);
            save = true;
            break;
        case sk_cleanup_copies:
            g_cleanup_copies = value == "1";
            save = true;
            break;
        case sk_cleanup_broken:
            g_cleanup_broken = value == "1";
            save = true;
            break;
        case sk_cleanup_mute:
            g_cleanup_mute = value == "1";
            save = true;
            break;
        case sk_snooze:
            g_snooze = _atoi64(value.c_str());
            save = true;
            break;
        case sk_close:
            if (value != "ask" && value != "tray" && value != "quit")
                return;
            g_close = value;
            window::set_close(value);
            save = true;
            break;
        case sk_steam:
            g_steam = value == "1";
            profiles::set_online(g_steam);
            save = true;
            break;
        case sk_autostart:
        case sk_desktop:
            if (!setup::state().installed)
                return;
            if (set_key(key) == sk_autostart)
                setup::set_autostart(value == "1");
            else
                setup::set_desktop(value == "1");
            send_settings();
            return;
        case sk_updates:
            g_updates = value == "1";
            if (g_updates)
                update::check();
            save = true;
            break;
        case sk_talk:
            g_talk = value == "1";
            save = true;
            talking = true;
            break;
        case sk_talk_style:
            g_talk_style = std::clamp(atoi(value.c_str()), 0, 1);
            save = true;
            talking = true;
            break;
        case sk_talk_lead:
            g_talk_lead = std::clamp(atoi(value.c_str()), 0, 640);
            save = true;
            talking = true;
            break;
        case sk_talk_min:
            g_talk_min = std::clamp(atoi(value.c_str()), 0, 640);
            save = true;
            talking = true;
            break;
        case sk_talk_refresh:
            g_talk_refresh = std::clamp(atoi(value.c_str()), 32, 1920);
            save = true;
            talking = true;
            break;
        default:
            return;
        }
        if (talking)
            launch::set_talk(g_talk, g_talk_style, g_talk_lead, g_talk_min, g_talk_refresh);
        if (save) {
            save_settings();
            send_settings();
        }
    }

    void cmd_cleanup_folder(const json_t& j) {
        std::string path = json::str(json::at(j, "path"));
        const json_t* flag = json::at(j, "on");
        if (flag && flag->b)
            g_skip.erase(path);
        else
            g_skip.insert(path);
        save_settings();
        send_settings();
    }

    void cmd_keep(const json_t& j) {
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
    }

    void cmd_cleanup(const json_t& j) {
        std::string method = json::str(json::at(j, "method"), g_cleanup_method);
        delete_demos(known_paths(json::at(j, "paths")), method != "delete");
    }

    void cmd_open_url(const json_t& j) {
        open_url(json::str(json::at(j, "url")));
    }

    void cmd_install(const json_t& j) {
        std::string why;
        const json_t* desk = json::at(j, "desktop");
        const json_t* boot = json::at(j, "autostart");
        if (!setup::install(desk && desk->b, why)) {
            web::post("{\"type\":\"setup_error\",\"err\":\"" + json::escape(why) + "\"}");
            return;
        }
        setup::set_autostart(boot && boot->b);
        if (!setup::state().here && setup::start_installed())
            window::command("quit");
        else
            send_settings();
    }

    void cmd_uninstall(const json_t& j) {
        const json_t* wipe = json::at(j, "data");
        setup::uninstall(wipe && wipe->b, g_data);
        window::command("quit");
    }

    void cmd_tray_text(const json_t& j) {
        window::set_menu(text::wide(json::str(json::at(j, "open"))), text::wide(json::str(json::at(j, "quit"))));
    }

    void cmd_win(const json_t& j) {
        window::command(json::str(json::at(j, "action")));
    }

    void cmd_storage(const json_t&) {
        send_storage();
    }

    void cmd_clear_unpacked(const json_t&) {
        launch::clear();
    }

    void cmd_clear_cache(const json_t&) {
        jobs::clear();
        perfs::clear();
        tracks::clear();
        profiles::clear();
        faceit::clear();
        maps::clear();
        inventory::clear();
        launch::clear();
        web::post("{\"type\":\"cache_cleared\"}");
        rescan();
        send_storage();
    }

    void cmd_watch(const json_t& j) {
        std::string path = json::str(json::at(j, "path"));
        const json_t* fix = json::at(j, "fix");
        const demo_file_t* f = file_of(fixed_path(path));
        bool repair = !f && fix && fix->kind == jk_bool && fix->b;
        if (!f)
            f = file_of(path);
        if (!f)
            return;
        std::vector<talk::talker_t> who;
        if (const demo_file_t* src = file_of(path); src && g_talk)
            jobs::talkers(*src, who);
        launch::watch(*f, (uint32_t)(int64_t)json::num(json::at(j, "low")), (uint32_t)(int64_t)json::num(json::at(j, "high")), repair, who);
    }

    void cmd_save_clip(const json_t& j) {
        save_clip(json::str(json::at(j, "name")), json::str(json::at(j, "data")));
    }

    void cmd_track(const json_t& j) {
        if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
            tracks::want(*f, true);
    }

    void cmd_radar(const json_t& j) {
        maps::radar(json::str(json::at(j, "map")));
    }

    void cmd_icons(const json_t&) {
        inventory::want_icons();
    }

    void cmd_econ_want(const json_t& j) {
        inventory::icons(strings(json::at(j, "icons")));
    }

    void cmd_skins(const json_t& j) {
        if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
            inventory::want(*f, json::str(json::at(j, "lang"), "en"));
    }

    void cmd_voice(const json_t& j) {
        if (const demo_file_t* f = file_of(json::str(json::at(j, "path"))))
            voices::want(*f, true);
    }

    void cmd_art(const json_t& j) {
        maps::ensure(json::str(json::at(j, "map")));
    }

    void cmd_steam(const json_t& j) {
        open_profile(json::str(json::at(j, "id")));
    }

    void cmd_faceit_profile(const json_t& j) {
        open_faceit(json::str(json::at(j, "nick")));
    }

    void cmd_delete_demos(const json_t& j) {
        delete_demos(known_paths(json::at(j, "paths")));
    }

    void cmd_move_demos(const json_t& j) {
        move_demos(known_paths(json::at(j, "paths")));
    }

    void cmd_copy_paths(const json_t& j) {
        std::string all;
        for (const std::string& p : known_paths(json::at(j, "paths")))
            all += (all.empty() ? "" : "\r\n") + p;
        if (!all.empty())
            copy(all);
    }

    void cmd_update_install(const json_t&) {
        update::install();
    }

    void cmd_reveal(const json_t& j) {
        reveal(json::str(json::at(j, "path")));
    }

    void cmd_fix_demo(const json_t& j) {
        std::string path = json::str(json::at(j, "path"));
        if (file_of(fixed_path(path)))
            web::post("{\"type\":\"fixed\",\"path\":\"" + json::escape(path) + "\",\"ok\":true,\"err\":\"\"}");
        else if (const demo_file_t* f = file_of(path))
            launch::prepare_fix(*f);
    }

    typedef void (*cmd_fn_t)(const json_t&);

    struct command_t {
        const char* name;
        cmd_fn_t run;
    };

    const command_t COMMANDS[] = {
        {"ready", cmd_ready},
        {"open", cmd_open},
        {"copy", cmd_copy},
        {"rescan", cmd_rescan},
        {"add_folder", cmd_add_folder},
        {"remove_folder", cmd_remove_folder},
        {"faceit", cmd_faceit},
        {"profiles", cmd_profiles},
        {"set", cmd_set},
        {"cleanup_folder", cmd_cleanup_folder},
        {"keep", cmd_keep},
        {"cleanup", cmd_cleanup},
        {"open_url", cmd_open_url},
        {"install", cmd_install},
        {"uninstall", cmd_uninstall},
        {"tray_text", cmd_tray_text},
        {"win", cmd_win},
        {"storage", cmd_storage},
        {"clear_unpacked", cmd_clear_unpacked},
        {"clear_cache", cmd_clear_cache},
        {"watch", cmd_watch},
        {"save_clip", cmd_save_clip},
        {"track", cmd_track},
        {"radar", cmd_radar},
        {"icons", cmd_icons},
        {"econ_want", cmd_econ_want},
        {"skins", cmd_skins},
        {"voice", cmd_voice},
        {"art", cmd_art},
        {"steam", cmd_steam},
        {"faceit_profile", cmd_faceit_profile},
        {"delete_demos", cmd_delete_demos},
        {"move_demos", cmd_move_demos},
        {"copy_paths", cmd_copy_paths},
        {"update_install", cmd_update_install},
        {"reveal", cmd_reveal},
        {"fix_demo", cmd_fix_demo},
    };

    cmd_fn_t command_for(std::string_view name) {
        for (const command_t& c : COMMANDS)
            if (name == c.name)
                return c.run;
        return nullptr;
    }

    void handle(const std::string& msg) {
        json_t j;
        std::string err;
        if (!json::parse(msg, j, err)) {
            dbg::print("[bridge] bad message: %s", err.c_str());
            return;
        }
        std::string_view type = json::view(json::at(j, "type"));
        cmd_fn_t run = command_for(type);
        if (run)
            run(j);
        else
            dbg::print("[bridge] unknown message %.*s", (int)type.size(), type.data());
    }
}