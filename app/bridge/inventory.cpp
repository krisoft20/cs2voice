#include "pch.h"

#include <objbase.h>

#include "bridge/inventory.h"
#include "bridge/jobs.h"
#include "dbg.h"
#include "game/art.h"
#include "game/econ.h"
#include "game/vpk.h"
#include "json/json.h"
#include "platform/files.h"
#include "platform/window.h"
#include "text.h"

namespace inventory {
    std::mutex g_mu;
    std::condition_variable g_cv;
    std::thread g_worker;
    std::atomic<bool> g_quit = false;
    demo_file_t g_want;
    std::string g_lang;
    bool g_pending = false;
    std::deque<std::pair<std::string, std::string>> g_icons;
    bool g_svgs_wanted = false;
    bool g_svgs_done = false;
    std::wstring g_svg_dir;
    std::wstring g_dir;
    std::string g_game;
    std::set<std::string> g_failed;

    std::string file_of(const std::string& icon) {
        std::string stem = icon;
        const char* head = "panorama/images/econ/";
        if (stem.rfind(head, 0) == 0)
            stem = stem.substr(strlen(head));
        size_t tail = stem.rfind("_png.vtex_c");
        if (tail != std::string::npos)
            stem = stem.substr(0, tail);
        for (char& c : stem)
            if (c == '/' || c == '\\')
                c = '_';
        return stem + ".png";
    }

    bool have(const std::string& file) {
        return GetFileAttributesW((g_dir + L"\\" + text::wide(file)).c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    std::string image(const std::string& icon, const std::string& base, std::vector<std::pair<std::string, std::string>>& todo) {
        if (icon.empty())
            return std::string();
        std::string file = file_of(icon);
        if (!have(file))
            todo.push_back({icon, base});
        return "econ/" + file;
    }

    std::string str(const std::string& s) {
        return "\"" + json::escape(s) + "\"";
    }

    std::string build(const demo_file_t& f, std::vector<std::pair<std::string, std::string>>& todo, std::string& err) {
        std::string info;
        bool stale = false;
        json_t r;
        if (!jobs::cached(f, info, stale) || !json::parse(info, r, err)) {
            err = err.empty() ? "demo not read yet" : err;
            return std::string();
        }
        static const std::vector<json_t> none;
        std::string out = "[";
        const json_t* players = json::at(r, "players");
        bool first = true;
        for (const json_t& p : players ? players->arr : none) {
            if (!first)
                out += ",";
            first = false;
            out += "{\"steamid\":" + str(json::str(json::at(p, "steamid"))) + ",\"music\":" + str(econ::music((int)json::num(json::at(p, "music")))) + ",\"items\":[";
            const json_t* skins = json::at(p, "skins");
            bool first_item = true;
            for (const json_t& s : skins ? skins->arr : none) {
                int def = (int)json::num(json::at(s, "def"));
                int paint = (int)json::num(json::at(s, "paint"));
                float wear = (float)json::num(json::at(s, "wear"));
                const json_t* g = json::at(s, "gloves");
                bool gloves = g && g->kind == jk_bool && g->b;
                econ_item_t it;
                if (!econ::item(def, paint, wear, gloves, it))
                    continue;
                if (!first_item)
                    out += ",";
                first_item = false;
                char num[32];
                snprintf(num, sizeof(num), "%.6f", wear);
                out += "{\"weapon\":" + str(it.weapon) + ",\"skin\":" + str(it.skin) + ",\"rarity\":" + str(it.rarity) + ",\"color\":" + str(it.color) +
                       ",\"slot\":" + str(it.slot) + ",\"sides\":" + std::to_string((int)json::num(json::at(s, "sides"))) + ",\"rank\":" + std::to_string(it.rank) + ",\"star\":" + (it.star ? "true" : "false") + ",\"gloves\":" + (gloves ? "true" : "false") +
                       ",\"paint\":" + std::to_string(paint) + ",\"wear\":" + num + ",\"wear_name\":" + str(paint > 0 ? econ::wear_name(wear) : std::string()) +
                       ",\"seed\":" + std::to_string((int)json::num(json::at(s, "seed"))) + ",\"st\":" + std::to_string((int)json::num(json::at(s, "st"))) +
                       ",\"tag\":" + str(json::str(json::at(s, "tag"))) + ",\"img\":" + str(image(it.icon, it.base_icon, todo)) + ",\"variants\":[";
                for (size_t v = 0; v < it.variants.size(); v++)
                    out += std::string(v ? "," : "") + "{\"icon\":" + str(it.variants[v]) + ",\"img\":" + str("econ/" + file_of(it.variants[v])) + "}";
                out += "],\"stickers\":[";
                const json_t* st = json::at(s, "stickers");
                bool first_sticker = true;
                for (const json_t& id : st ? st->arr : none) {
                    std::string name, icon;
                    if (!econ::sticker((uint32_t)json::num(&id), name, icon))
                        continue;
                    out += std::string(first_sticker ? "" : ",") + "{\"name\":" + str(name) + ",\"img\":" + str(image(icon, std::string(), todo)) + "}";
                    first_sticker = false;
                }
                out += "]}";
            }
            out += "]}";
        }
        return out + "]";
    }

    bool make(const std::string& pak, const std::string& icon, const std::string& base) {
        std::string file = file_of(icon);
        if (have(file))
            return true;
        if (g_failed.count(icon))
            return false;
        std::string out = text::utf8(g_dir) + "\\" + file, e;
        bool ok = art::texture_png(pak, icon, out, e);
        if (!ok && !base.empty())
            ok = art::texture_png(pak, base, out, e);
        if (!ok) {
            g_failed.insert(icon);
            dbg::print("[inventory] %s: %s", icon.c_str(), e.c_str());
        }
        return ok;
    }

    void svgs() {
        std::string pak = g_game + "\\pak01_dir.vpk", err;
        const char* dirs[] = {"panorama/images/icons/equipment/", "panorama/images/hud/deathnotice/", "panorama/images/icons/ui/"};
        const char* ui[] = {"elimination", "kill", "bomb", "defuser", "clock"};
        int made = 0;
        uint64_t t0 = GetTickCount64();
        CreateDirectoryW(g_svg_dir.c_str(), nullptr);
        for (const char* d : dirs) {
            std::vector<std::string> files;
            if (!vpk::list(pak, d, files, err)) {
                dbg::print("[inventory] svg list: %s", err.c_str());
                continue;
            }
            std::string group = std::string(d).substr(16);
            group = group.substr(0, group.size() - 1);
            for (char& c : group)
                if (c == '/')
                    c = '_';
            for (const std::string& f : files) {
                std::string name = f.substr(strlen(d));
                if (name.size() < 8 || name.compare(name.size() - 7, 7, ".vsvg_c") || name.find('/') != std::string::npos)
                    continue;
                name = name.substr(0, name.size() - 7);
                if (!strcmp(d, dirs[2]) && std::find(std::begin(ui), std::end(ui), name) == std::end(ui))
                    continue;
                std::wstring out = g_svg_dir + L"\\" + text::wide(group + "_" + name + ".svg");
                if (GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES)
                    continue;
                std::vector<uint8_t> raw;
                if (!vpk::extract(pak, f, raw, err))
                    continue;
                std::string body(raw.begin(), raw.end());
                size_t a = body.find("<svg"), b = body.rfind("</svg>");
                if (a == std::string::npos || b == std::string::npos || b < a)
                    continue;
                FILE* fp = _wfopen(out.c_str(), L"wb");
                if (!fp)
                    continue;
                fwrite(body.data() + a, 1, b + 6 - a, fp);
                fclose(fp);
                made++;
            }
        }
        dbg::print("[inventory] %d icons cut in %llu ms", made, (unsigned long long)(GetTickCount64() - t0));
    }

    void work() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (true) {
            demo_file_t f;
            std::string lang;
            std::pair<std::string, std::string> icon;
            bool skins = false;
            {
                std::unique_lock<std::mutex> lock(g_mu);
                g_cv.wait(lock, [] { return g_quit || g_pending || !g_icons.empty() || (g_svgs_wanted && !g_svgs_done); });
                if (g_quit)
                    break;
                if (g_svgs_wanted && !g_svgs_done) {
                    g_svgs_done = true;
                    lock.unlock();
                    if (!g_game.empty())
                        svgs();
                    window::post_json("{\"type\":\"icons\"}");
                    continue;
                }
                if (g_pending) {
                    f = g_want;
                    lang = g_lang;
                    g_pending = false;
                    skins = true;
                } else {
                    icon = g_icons.front();
                    g_icons.pop_front();
                }
            }
            std::string pak = g_game + "\\pak01_dir.vpk";
            if (!skins) {
                if (!g_game.empty() && make(pak, icon.first, icon.second))
                    window::post_json("{\"type\":\"econ_img\",\"img\":" + str("econ/" + file_of(icon.first)) + "}");
                continue;
            }
            uint64_t t0 = GetTickCount64();
            std::string err, players;
            std::vector<std::pair<std::string, std::string>> todo;
            if (g_game.empty())
                err = "cs2 not found";
            else if (econ::load(g_game, lang, err))
                players = build(f, todo, err);
            window::post_json("{\"type\":\"skins\",\"path\":" + str(f.path) + ",\"err\":" + str(err) + ",\"players\":" + (players.empty() ? "[]" : players) + "}");
            {
                std::lock_guard<std::mutex> lock(g_mu);
                for (const auto& t : todo)
                    g_icons.push_back(t);
            }
            dbg::print("[inventory] %s in %llu ms, %zu images to make", f.name.c_str(), (unsigned long long)(GetTickCount64() - t0), todo.size());
        }
        CoUninitialize();
    }

    void want_icons() {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            if (g_svgs_done) {
                window::post_json("{\"type\":\"icons\"}");
                return;
            }
            g_svgs_wanted = true;
        }
        g_cv.notify_all();
    }

    bool init(const std::wstring& dir) {
        g_dir = dir;
        g_svg_dir = dir.substr(0, dir.rfind(L'\\')) + L"\\icons";
        CreateDirectoryW(dir.c_str(), nullptr);
        g_game = finder::game_dir();
        g_worker = std::thread(work);
        SetThreadPriority(g_worker.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
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
        std::lock_guard<std::mutex> lock(g_mu);
        files::wipe(g_dir, L"*.png");
        g_failed.clear();
        files::wipe(g_svg_dir, L"*.svg");
        g_svgs_done = false;
    }

    void icons(const std::vector<std::string>& list) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (auto it = list.rbegin(); it != list.rend(); ++it)
                if (it->rfind("panorama/images/econ/", 0) == 0 && it->find("..") == std::string::npos)
                    g_icons.push_front({*it, std::string()});
        }
        g_cv.notify_all();
    }

    void want(const demo_file_t& f, const std::string& lang) {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_want = f;
            g_lang = lang;
            g_pending = true;
        }
        g_cv.notify_all();
    }
}
