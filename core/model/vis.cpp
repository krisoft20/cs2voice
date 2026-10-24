#include "pch.h"

#include <thread>
#include <windows.h>

#include "model/vis.h"

static const float smoke_radius = 144.f;

static fvec_t eye_of(const pstate_t& s) {
    return {s.pos[0], s.pos[1], s.pos[2] + s.eye_z};
}

static fvec_t chest_of(const pstate_t& s) {
    return {s.pos[0], s.pos[1], s.pos[2] + (s.eye_z > 0 ? s.eye_z : 64.f) - 18.f};
}

static bool sees(const match_t& m, const map_geo_t& g, int t, const pstate_t& sp, const pstate_t& se, uint32_t* hints) {
    fvec_t eye = eye_of(sp);
    fvec_t feet = {se.pos[0], se.pos[1], se.pos[2]};
    if (!geo::body_visible(g, eye, feet, se.eye_z > 0 ? se.eye_z : 64.f, hints))
        return false;
    return !vis::smoked(m, t, eye, chest_of(se));
}

static void build_range(vis_t& v, const match_t& m, const map_geo_t& g, int from, int to) {
    std::vector<uint32_t> hints(64 * 64 * 2, ~0u);
    for (int k = from; k < to; k++) {
        int t = k * v.stride;
        if (t >= (int)m.live.size() || !m.live[t])
            continue;
        for (const player_t& p : m.players) {
            if (p.slot < 0)
                continue;
            const pstate_t* sp = model::at(m, p.slot, t);
            if (!sp || !(sp->flags & pf_alive))
                continue;
            uint64_t mask = 0;
            for (const player_t& e : m.players) {
                if (e.slot < 0 || e.slot == p.slot)
                    continue;
                const pstate_t* se = model::at(m, e.slot, t);
                if (!se || !(se->flags & pf_alive) || se->team == sp->team)
                    continue;
                if (sees(m, g, t, *sp, *se, &hints[(p.slot * 64 + e.slot) * 2]))
                    mask |= 1ull << e.slot;
            }
            v.los[(size_t)k * 64 + p.slot] = mask;
        }
    }
}

namespace vis {

static std::atomic<int> g_threads = 0;
static std::atomic<bool> g_idle = false;

void set_threads(int n, bool idle) {
    g_threads = n < 0 ? 0 : n;
    g_idle = idle;
}

bool smoked(const match_t& m, int tick, const fvec_t& a, const fvec_t& b) {
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float len2 = dx * dx + dy * dy + dz * dz;
    for (const smoke_t& sm : m.smokes) {
        if (tick < sm.start || tick > sm.end)
            continue;
        float t = len2 > 0 ? ((sm.x - a.x) * dx + (sm.y - a.y) * dy + (sm.z - a.z) * dz) / len2 : 0.f;
        if (t < 0)
            t = 0;
        if (t > 1)
            t = 1;
        float cx = a.x + t * dx - sm.x, cy = a.y + t * dy - sm.y, cz = a.z + t * dz - sm.z;
        if (cx * cx + cy * cy + cz * cz < smoke_radius * smoke_radius)
            return true;
    }
    return false;
}

void build(vis_t& v, const match_t& m, const map_geo_t& g, int stride) {
    v.stride = stride;
    v.ticks = m.max_tick / stride + 1;
    v.los.assign((size_t)v.ticks * 64, 0);
    unsigned n = g_threads ? (unsigned)g_threads : std::thread::hardware_concurrency();
    if (!n)
        n = 4;
    int per = (v.ticks + (int)n - 1) / (int)n;
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < n; i++) {
        int from = (int)i * per;
        int to = std::min(v.ticks, from + per);
        if (from >= to)
            break;
        threads.emplace_back([&v, &m, &g, from, to] {
            if (g_idle)
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
            build_range(v, m, g, from, to);
        });
    }
    for (std::thread& t : threads)
        t.join();
}

bool visible(const match_t& m, const map_geo_t& g, int tick, int viewer, int target) {
    const pstate_t* sp = model::at(m, viewer, tick);
    const pstate_t* se = model::at(m, target, tick);
    if (!sp || !se || !(sp->flags & se->flags & pf_alive) || sp->team == se->team)
        return false;
    return sees(m, g, tick, *sp, *se, nullptr);
}

uint64_t seen(const vis_t& v, int tick, int slot) {
    int k = tick / v.stride;
    if (k < 0 || k >= v.ticks || slot < 0 || slot >= 64)
        return 0;
    return v.los[(size_t)k * 64 + slot];
}

}
