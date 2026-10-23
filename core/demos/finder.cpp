#include "pch.h"

#include <windows.h>
#include <shlobj.h>

#include "demos/finder.h"
#include "text.h"

namespace finder {
    const int MAX_DEPTH = 2;
    const char* GAME = "\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo";
    const char* REPLAYS = "\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo\\replays";

    bool is_dir(const std::string& path) {
        DWORD a = GetFileAttributesW(text::wide(path).c_str());
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    }

    static bool read_file(const std::string& path, std::string& out) {
        FILE* f = _wfopen(text::wide(path).c_str(), L"rb");
        if (!f)
            return false;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            out.append(buf, n);
        fclose(f);
        return true;
    }

    std::string steam_root() {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};
        wchar_t buf[MAX_PATH] = {};
        DWORD size = sizeof(buf);
        DWORD type = 0;
        LSTATUS st = RegQueryValueExW(key, L"SteamPath", nullptr, &type, (BYTE*)buf, &size);
        RegCloseKey(key);
        if (st != ERROR_SUCCESS || type != REG_SZ)
            return {};
        std::string path = text::utf8(buf);
        for (char& c : path)
            if (c == '/')
                c = '\\';
        return path;
    }

    void libraries(const std::string& root, std::vector<std::string>& out) {
        out.push_back(root);
        std::string vdf;
        if (!read_file(root + "\\steamapps\\libraryfolders.vdf", vdf))
            return;
        size_t at = 0;
        while ((at = vdf.find("\"path\"", at)) != std::string::npos) {
            size_t open = vdf.find('"', at + 6);
            if (open == std::string::npos)
                break;
            std::string lib;
            size_t i = open + 1;
            for (; i < vdf.size() && vdf[i] != '"'; i++) {
                if (vdf[i] == '\\' && i + 1 < vdf.size())
                    i++;
                lib += vdf[i];
            }
            at = i;
            bool seen = false;
            for (const std::string& o : out)
                seen |= text::lower(o) == text::lower(lib);
            if (!seen)
                out.push_back(lib);
        }
    }

    uint64_t current_user() {
        const uint64_t BASE = 76561197960265728ull;
        DWORD id = 0, size = sizeof(id);
        if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser", RRF_RT_REG_DWORD, nullptr, &id, &size) ==
                ERROR_SUCCESS &&
            id)
            return BASE + id;
        std::string root = steam_root();
        std::string vdf;
        if (root.empty() || !read_file(root + "\\config\\loginusers.vdf", vdf))
            return 0;
        uint64_t last = 0;
        for (size_t at = 0; at < vdf.size(); at++) {
            if (vdf[at] == '"' && at + 18 < vdf.size() && vdf.compare(at + 1, 4, "7656") == 0)
                last = strtoull(vdf.c_str() + at + 1, nullptr, 10);
            if (vdf.compare(at, 12, "\"MostRecent\"") == 0) {
                size_t v = vdf.find('"', at + 12);
                if (v != std::string::npos && vdf[v + 1] == '1')
                    return last;
            }
        }
        return 0;
    }

    std::string game_dir() {
        std::string root = steam_root();
        if (root.empty())
            return {};
        std::vector<std::string> libs;
        libraries(root, libs);
        for (const std::string& lib : libs) {
            DWORD a = GetFileAttributesW(text::wide(lib + GAME + "\\pak01_dir.vpk").c_str());
            if (a != INVALID_FILE_ATTRIBUTES)
                return lib + GAME;
        }
        return {};
    }

    std::string downloads() {
        PWSTR path = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path) != S_OK)
            return {};
        std::string out = text::utf8(path);
        CoTaskMemFree(path);
        return out;
    }

    void default_folders(std::vector<std::string>& out) {
        std::string root = steam_root();
        if (!root.empty()) {
            std::vector<std::string> libs;
            libraries(root, libs);
            for (const std::string& lib : libs)
                if (is_dir(lib + REPLAYS))
                    out.push_back(lib + REPLAYS);
        }
        std::string dl = downloads();
        if (is_dir(dl))
            out.push_back(dl);
    }

    bool ends_with(const std::string& s, const char* tail) {
        size_t n = strlen(tail);
        return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
    }

    bool is_demo(const std::string& name) {
        std::string l = text::lower(name);
        return ends_with(l, ".dem") || ends_with(l, ".dem.zst");
    }

    int64_t unix_time(const FILETIME& ft) {
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return (int64_t)(t / 10000000ull) - 11644473600ll;
    }

    void walk(const std::string& dir, const std::string& folder, int depth, std::vector<demo_file_t>& out) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(text::wide(dir + "\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE)
            return;
        do {
            std::string name = text::utf8(fd.cFileName);
            if (name == "." || name == "..")
                continue;
            std::string path = dir + "\\" + name;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (depth < MAX_DEPTH && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    walk(path, folder, depth + 1, out);
                continue;
            }
            if (!is_demo(name))
                continue;
            demo_file_t d;
            d.path = path;
            d.name = name;
            d.folder = folder;
            d.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            d.mtime = unix_time(fd.ftLastWriteTime);
            out.push_back(d);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    void scan(const std::vector<std::string>& folders, std::vector<demo_file_t>& out) {
        out.clear();
        for (const std::string& f : folders)
            walk(f, f, 0, out);
        std::sort(out.begin(), out.end(), [](const demo_file_t& a, const demo_file_t& b) { return a.mtime > b.mtime; });
    }
}
