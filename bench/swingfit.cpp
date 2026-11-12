#include "pch.h"

#include <array>
#include <atomic>
#include <mutex>
#include <thread>

#include "demos/reader.h"
#include "demos/swing.h"

struct fit_t {
    double cell[2][6][6] = {};
    double eco[2] = {};
    double clock[2] = {};
    double seen[2][6][6] = {};
    std::vector<std::array<double, 2>> map;
};

struct demo_samples_t {
    std::vector<swing_sample_t> rows;
    std::string map;
    int rounds = 0;
};

static const double PRIOR = 300;
static const double MAP_PRIOR = 600;
static const int WORKERS = 6;

static double sigmoid(double z) {
    return 1.0 / (1.0 + exp(-z));
}

static double logit(double p) {
    p = std::clamp(p, 1e-4, 1.0 - 1e-4);
    return log(p / (1.0 - p));
}

static bool solve(double a[3][4], int n) {
    for (int i = 0; i < n; i++) {
        int best = i;
        for (int r = i + 1; r < n; r++)
            if (fabs(a[r][i]) > fabs(a[best][i]))
                best = r;
        if (fabs(a[best][i]) < 1e-12)
            return false;
        for (int c = 0; c <= n; c++)
            std::swap(a[i][c], a[best][c]);
        for (int r = 0; r < n; r++) {
            if (r == i)
                continue;
            double f = a[r][i] / a[i][i];
            for (int c = i; c <= n; c++)
                a[r][c] -= f * a[i][c];
        }
    }
    for (int i = 0; i < n; i++)
        a[i][n] /= a[i][i];
    return true;
}

template <typename feat_t, typename base_t>
static void newton(const std::vector<swing_sample_t>& rows, int planted, int n, double* w, feat_t feat, base_t base) {
    for (int it = 0; it < 25; it++) {
        double a[3][4] = {};
        for (const swing_sample_t& r : rows) {
            if ((int)r.state.planted != planted)
                continue;
            double x[3];
            feat(r.state, x);
            double z = base(r.state);
            for (int i = 0; i < n; i++)
                z += w[i] * x[i];
            double p = sigmoid(z), h = p * (1 - p);
            for (int i = 0; i < n; i++) {
                a[i][n] += ((r.ct_won ? 1.0 : 0.0) - p) * x[i];
                for (int j = 0; j < n; j++)
                    a[i][j] += h * x[i] * x[j];
            }
        }
        for (int i = 0; i < n; i++)
            a[i][i] += 1e-6;
        if (!solve(a, n))
            return;
        double moved = 0;
        for (int i = 0; i < n; i++) {
            w[i] += a[i][n];
            moved += fabs(a[i][n]);
        }
        if (moved < 1e-7)
            return;
    }
}

static bool open_cell(int b, int ct, int t) {
    return ct >= 1 && (b ? t >= 0 : t >= 1);
}

static void order(fit_t& f) {
    for (int pass = 0; pass < 200; pass++) {
        bool moved = false;
        for (int b = 0; b < 2; b++)
            for (int ct = 1; ct <= 5; ct++)
                for (int t = 0; t <= 5; t++) {
                    if (!open_cell(b, ct, t))
                        continue;
                    int pairs[2][2] = {{ct + 1, t}, {ct, t - 1}};
                    for (auto& o : pairs) {
                        if (o[0] > 5 || !open_cell(b, o[0], o[1]) || f.cell[b][o[0]][o[1]] >= f.cell[b][ct][t] - 1e-9)
                            continue;
                        double wa = f.seen[b][ct][t] + PRIOR, wb = f.seen[b][o[0]][o[1]] + PRIOR;
                        double mean = (f.cell[b][ct][t] * wa + f.cell[b][o[0]][o[1]] * wb) / (wa + wb);
                        f.cell[b][ct][t] = f.cell[b][o[0]][o[1]] = mean;
                        moved = true;
                    }
                }
        if (!moved)
            return;
    }
}

static double score(const fit_t& f, const swing_state_t& s) {
    int b = s.planted ? 1 : 0;
    double map = s.map >= 0 && s.map < (int)f.map.size() ? f.map[s.map][b] : 0;
    return f.cell[b][std::min(s.ct, 5)][std::min(s.t, 5)] + f.eco[b] * s.eco + f.clock[b] * s.clock + map;
}

static fit_t fit(const std::vector<swing_sample_t>& rows, size_t maps) {
    fit_t f;
    f.map.assign(maps, {0.0, 0.0});
    double smooth[2][3] = {};
    double won[2][6][6] = {};
    for (const swing_sample_t& r : rows) {
        f.seen[r.state.planted][r.state.ct][r.state.t]++;
        won[r.state.planted][r.state.ct][r.state.t] += r.ct_won;
    }
    for (int b = 0; b < 2; b++) {
        newton(
            rows, b, 3, smooth[b], [](const swing_state_t& s, double* x) { x[0] = 1, x[1] = s.ct, x[2] = s.t; }, [](const swing_state_t&) { return 0.0; });
        for (int ct = 0; ct <= 5; ct++)
            for (int t = 0; t <= 5; t++) {
                double prior = sigmoid(smooth[b][0] + smooth[b][1] * ct + smooth[b][2] * t);
                f.cell[b][ct][t] = logit((won[b][ct][t] + PRIOR * prior) / (f.seen[b][ct][t] + PRIOR));
            }
    }
    order(f);
    for (int round = 0; round < 8; round++) {
        for (int b = 0; b < 2; b++) {
            double w[2] = {f.eco[b], f.clock[b]};
            newton(
                rows, b, 2, w, [](const swing_state_t& s, double* x) { x[0] = s.eco, x[1] = s.clock; },
                [&](const swing_state_t& s) { return f.cell[s.planted][s.ct][s.t] + (s.map >= 0 && s.map < (int)f.map.size() ? f.map[s.map][s.planted] : 0); });
            f.eco[b] = w[0];
            f.clock[b] = w[1];
        }
        for (int it = 0; it < 10; it++) {
            double g[2][6][6] = {}, h[2][6][6] = {};
            for (const swing_sample_t& r : rows) {
                double p = sigmoid(score(f, r.state));
                g[r.state.planted][r.state.ct][r.state.t] += (r.ct_won ? 1.0 : 0.0) - p;
                h[r.state.planted][r.state.ct][r.state.t] += p * (1 - p);
            }
            for (int b = 0; b < 2; b++)
                for (int ct = 1; ct <= 5; ct++)
                    for (int t = 0; t <= 5; t++) {
                        if (!open_cell(b, ct, t))
                            continue;
                        double prior = sigmoid(smooth[b][0] + smooth[b][1] * ct + smooth[b][2] * t);
                        double at = sigmoid(f.cell[b][ct][t]);
                        double grad = g[b][ct][t] + PRIOR * (prior - at);
                        double hess = h[b][ct][t] + PRIOR * at * (1 - at) + 1e-9;
                        f.cell[b][ct][t] += std::clamp(grad / hess, -1.0, 1.0);
                    }
        }
        order(f);
        for (int it = 0; it < 5; it++) {
            std::vector<std::array<double, 2>> g(maps, {0.0, 0.0}), h(maps, {0.0, 0.0});
            for (const swing_sample_t& r : rows) {
                if (r.state.map < 0 || r.state.map >= (int)maps)
                    continue;
                double p = sigmoid(score(f, r.state));
                g[r.state.map][r.state.planted] += (r.ct_won ? 1.0 : 0.0) - p;
                h[r.state.map][r.state.planted] += p * (1 - p);
            }
            for (size_t m = 0; m < maps; m++)
                for (int b = 0; b < 2; b++)
                    f.map[m][b] += (g[m][b] - MAP_PRIOR * f.map[m][b]) / (h[m][b] + MAP_PRIOR);
        }
    }
    return f;
}

static double loss(const fit_t& f, const std::vector<swing_sample_t>& rows, bool flat) {
    double sum = 0, base = 0;
    for (const swing_sample_t& r : rows)
        base += r.ct_won;
    base /= std::max<size_t>(1, rows.size());
    for (const swing_sample_t& r : rows) {
        double p = std::clamp(flat ? base : sigmoid(score(f, r.state)), 1e-6, 1 - 1e-6);
        sum -= r.ct_won ? log(p) : log(1 - p);
    }
    return sum / std::max<size_t>(1, rows.size());
}

static void calibration(const fit_t& f, const std::vector<swing_sample_t>& rows) {
    double n[10] = {}, said[10] = {}, won[10] = {};
    for (const swing_sample_t& r : rows) {
        double p = sigmoid(score(f, r.state));
        int k = std::min(9, (int)(p * 10));
        n[k]++;
        said[k] += p;
        won[k] += r.ct_won;
    }
    printf("  calibration (predicted -> actual, samples)\n");
    for (int k = 0; k < 10; k++)
        if (n[k] > 0)
            printf("    %5.1f%% -> %5.1f%%  %8.0f\n", 100 * said[k] / n[k], 100 * won[k] / n[k], n[k]);
    for (int b = 0; b < 2; b++) {
        double cn[4] = {}, cs[4] = {}, cw[4] = {};
        for (const swing_sample_t& r : rows) {
            if ((int)r.state.planted != b)
                continue;
            int k = std::min(3, (int)(r.state.clock * 4));
            cn[k]++;
            cs[k] += sigmoid(score(f, r.state));
            cw[k] += r.ct_won;
        }
        printf("  %s by clock quarter:", b ? "planted" : "open");
        for (int k = 0; k < 4; k++)
            printf("  %.1f%%->%.1f%%", cn[k] ? 100 * cs[k] / cn[k] : 0, cn[k] ? 100 * cw[k] / cn[k] : 0);
        printf("\n");
    }
}

static void show(const fit_t& f) {
    for (int b = 0; b < 2; b++) {
        printf("  %s: ct win %% at even economy, clock 0 (samples)\n        ", b ? "planted" : "open");
        for (int t = 0; t <= 5; t++)
            printf("   t=%d          ", t);
        printf("\n");
        for (int ct = 1; ct <= 5; ct++) {
            printf("    ct=%d", ct);
            for (int t = 0; t <= 5; t++)
                if (open_cell(b, ct, t))
                    printf("  %5.1f (%6.0f)", 100 * sigmoid(f.cell[b][ct][t]), f.seen[b][ct][t]);
                else
                    printf("  %14s", "-");
            printf("\n");
        }
        printf("    eco %.4f  clock %.4f\n", f.eco[b], f.clock[b]);
    }
}

static bool write(const fit_t& f, const std::vector<std::string>& maps, int rounds, const char* path) {
    FILE* o = fopen(path, "wb");
    if (!o)
        return false;
    fprintf(o, "#pragma once\n\n// generated by: bench swing <demo list> <this file>\n");
    fprintf(o, "// log odds of the ct side winning the round, [planted][ct alive][t alive]\n\n");
    fprintf(o, "const int SWING_ROUNDS = %d;\n\nconst float SWING_CELL[2][6][6] = {\n", rounds);
    for (int b = 0; b < 2; b++) {
        fprintf(o, "    {\n");
        for (int ct = 0; ct <= 5; ct++) {
            fprintf(o, "        {");
            for (int t = 0; t <= 5; t++)
                fprintf(o, "%s%.4ff", t ? ", " : "", open_cell(b, ct, t) ? f.cell[b][ct][t] : 0.0);
            fprintf(o, "},\n");
        }
        fprintf(o, "    },\n");
    }
    fprintf(o, "};\n\nconst float SWING_ECO[2] = {%.4ff, %.4ff};\nconst float SWING_CLOCK[2] = {%.4ff, %.4ff};\n", f.eco[0], f.eco[1], f.clock[0], f.clock[1]);
    fprintf(o, "\nconst int SWING_MAP_COUNT = %zu;\nconst char* const SWING_MAPS[] = {%s", std::max<size_t>(1, maps.size()), maps.empty() ? "\"\"" : "");
    for (size_t m = 0; m < maps.size(); m++)
        fprintf(o, "%s\"%s\"", m ? ", " : "", maps[m].c_str());
    fprintf(o, "};\nconst float SWING_MAP[][2] = {%s", maps.empty() ? "{0.f, 0.f}" : "");
    for (size_t m = 0; m < maps.size(); m++)
        fprintf(o, "%s{%.4ff, %.4ff}", m ? ", " : "", f.map[m][0], f.map[m][1]);
    fprintf(o, "};\n");
    fclose(o);
    return true;
}

static demo_samples_t collect(const std::string& path) {
    demo_samples_t d;
    roster_t r;
    if (!reader::read(path, r) || r.partial)
        return d;
    swing_view_t v;
    for (roster_player_t& p : r.players)
        if (p.slot >= 0 && p.slot < stats_t::SLOTS)
            v.players[p.slot] = &p.stats;
    v.winners = &r.winners;
    v.infos = &r.rounds_info;
    v.interval = r.tick_interval;
    if (v.interval <= 0)
        return d;
    swing::samples(v, d.rows);
    d.map = r.map;
    d.rounds = (int)r.winners.size();
    return d;
}

int swing_fit(const char* list, const char* out) {
    std::vector<std::string> paths;
    FILE* f = fopen(list, "rb");
    if (!f) {
        printf("cant open %s\n", list);
        return 1;
    }
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (!s.empty())
            paths.push_back(s);
    }
    fclose(f);

    std::vector<demo_samples_t> demos(paths.size());
    std::atomic<size_t> at = 0;
    std::mutex mu;
    std::vector<std::thread> pool;
    for (int w = 0; w < WORKERS; w++)
        pool.emplace_back([&] {
            for (size_t i = at++; i < paths.size(); i = at++) {
                demos[i] = collect(paths[i]);
                std::lock_guard<std::mutex> lock(mu);
                printf("  %3zu/%zu %6zu samples  %s\n", i + 1, paths.size(), demos[i].rows.size(), paths[i].c_str());
            }
        });
    for (std::thread& t : pool)
        t.join();

    std::vector<swing_sample_t> all, train, test;
    std::vector<std::string> maps;
    std::vector<int> per_map;
    int rounds = 0, used = 0;
    for (size_t i = 0; i < demos.size(); i++) {
        if (demos[i].rows.empty())
            continue;
        size_t m = std::find(maps.begin(), maps.end(), demos[i].map) - maps.begin();
        if (m == maps.size()) {
            maps.push_back(demos[i].map);
            per_map.push_back(0);
        }
        per_map[m] += demos[i].rounds;
        for (swing_sample_t& r : demos[i].rows)
            r.state.map = (int)m;
        used++;
        rounds += demos[i].rounds;
        all.insert(all.end(), demos[i].rows.begin(), demos[i].rows.end());
        std::vector<swing_sample_t>& part = used % 2 ? train : test;
        part.insert(part.end(), demos[i].rows.begin(), demos[i].rows.end());
    }
    printf("\n%d demos, %d rounds, %zu samples\n", used, rounds, all.size());
    if (all.empty())
        return 1;

    fit_t flat = fit(train, 0);
    printf("\nheld out half without maps: log loss %.4f\n", loss(flat, test, false));
    fit_t half = fit(train, maps.size());
    printf("\nheld out half: log loss %.4f, flat guess %.4f\n", loss(half, test, false), loss(half, test, true));
    calibration(half, test);

    bool by_map = loss(half, test, false) < loss(flat, test, false);
    printf("map offsets %s on the held out half\n", by_map ? "help" : "do not help");
    if (!by_map)
        maps.clear();
    fit_t full = fit(all, maps.size());
    printf("\nall demos: log loss %.4f\n", loss(full, all, false));
    show(full);
    for (size_t m = 0; m < maps.size(); m++)
        printf("  %-14s %4d rounds  open %+.3f  planted %+.3f\n", maps[m].c_str(), per_map[m], full.map[m][0], full.map[m][1]);
    if (!write(full, maps, rounds, out)) {
        printf("cant write %s\n", out);
        return 1;
    }
    printf("\nwrote %s\n", out);
    return 0;
}
