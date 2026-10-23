#include "pch.h"

#include <mutex>

#include "parser/sendtables/field_path.h"

enum fp_op_t {
    op_plus_one,
    op_plus_two,
    op_plus_three,
    op_plus_four,
    op_plus_n,
    op_push_one_left_delta_zero_right_zero,
    op_push_one_left_delta_zero_right_non_zero,
    op_push_one_left_delta_one_right_zero,
    op_push_one_left_delta_one_right_non_zero,
    op_push_one_left_delta_n_right_zero,
    op_push_one_left_delta_n_right_non_zero,
    op_push_one_left_delta_n_right_non_zero_pack6,
    op_push_one_left_delta_n_right_non_zero_pack8,
    op_push_two_left_delta_zero,
    op_push_two_pack5_left_delta_zero,
    op_push_three_left_delta_zero,
    op_push_three_pack5_left_delta_zero,
    op_push_two_left_delta_one,
    op_push_two_pack5_left_delta_one,
    op_push_three_left_delta_one,
    op_push_three_pack5_left_delta_one,
    op_push_two_left_delta_n,
    op_push_two_pack5_left_delta_n,
    op_push_three_left_delta_n,
    op_push_three_pack5_left_delta_n,
    op_push_n,
    op_push_n_and_non_topological,
    op_pop_one_plus_one,
    op_pop_one_plus_n,
    op_pop_all_but_one_plus_one,
    op_pop_all_but_one_plus_n,
    op_pop_all_but_one_plus_n_pack3,
    op_pop_all_but_one_plus_n_pack6,
    op_pop_n_plus_one,
    op_pop_n_plus_n,
    op_pop_n_and_non_topographical,
    op_non_topo_complex,
    op_non_topo_penultimate_plus_one,
    op_non_topo_complex_pack4,
    op_finish,
    op_count
};

static const uint32_t g_weights[op_count] = {
    36271, 10334, 1375, 646, 4128, 35, 3, 521, 2942, 560, 471, 10530, 251, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 310, 2, 0, 1837, 149, 300, 634, 0, 0, 1, 76, 271, 99, 25474,
};

static const int g_table_bits = 8;

static std::vector<int32_t> g_left, g_right;
static int32_t g_root = 0;
static int g_len[op_count];
static uint16_t g_table[1 << g_table_bits];
static int32_t g_table_node[1 << g_table_bits];
static bool g_ready = false;
static std::mutex g_init_mu;

namespace field_path {

void init() {
    std::lock_guard<std::mutex> lock(g_init_mu);
    if (g_ready)
        return;

    struct node_t {
        int weight;
        int value;
        int left;
        int right;
    };
    std::vector<node_t> pool;
    std::vector<int> h;
    for (int i = 0; i < op_count; i++) {
        int w = g_weights[i] ? (int)g_weights[i] : 1;
        pool.push_back({w, i, -1, -1});
        h.push_back((int)pool.size() - 1);
    }

    auto less = [&](int i, int j) {
        const node_t& a = pool[h[i]];
        const node_t& b = pool[h[j]];
        if (a.weight == b.weight)
            return a.value >= b.value;
        return a.weight < b.weight;
    };
    auto down = [&](int i, int n) {
        for (;;) {
            int j1 = 2 * i + 1;
            if (j1 >= n)
                break;
            int j = j1;
            if (j1 + 1 < n && less(j1 + 1, j1))
                j = j1 + 1;
            if (!less(j, i))
                break;
            std::swap(h[i], h[j]);
            i = j;
        }
    };
    auto up = [&](int j) {
        for (;;) {
            int i = (j - 1) / 2;
            if (i == j || !less(j, i))
                break;
            std::swap(h[i], h[j]);
            j = i;
        }
    };
    auto pop = [&]() {
        int n = (int)h.size() - 1;
        std::swap(h[0], h[n]);
        down(0, n);
        int top = h.back();
        h.pop_back();
        return top;
    };

    int n0 = (int)h.size();
    for (int i = n0 / 2 - 1; i >= 0; i--)
        down(i, n0);

    int next_value = op_count;
    while (h.size() > 1) {
        int a = pop();
        int b = pop();
        pool.push_back({pool[a].weight + pool[b].weight, next_value++, a, b});
        h.push_back((int)pool.size() - 1);
        up((int)h.size() - 1);
    }

    auto ref = [&](int idx) -> int32_t {
        const node_t& n = pool[idx];
        return n.left < 0 ? -(n.value + 1) : idx;
    };
    g_left.assign(pool.size(), 0);
    g_right.assign(pool.size(), 0);
    for (size_t i = 0; i < pool.size(); i++) {
        if (pool[i].left < 0)
            continue;
        g_left[i] = ref(pool[i].left);
        g_right[i] = ref(pool[i].right);
    }
    g_root = ref(h[0]);

    for (int idx = 0; idx < (1 << g_table_bits); idx++) {
        int32_t node = g_root;
        int d = 0;
        while (node >= 0 && d < g_table_bits) {
            node = ((idx >> d) & 1) ? g_right[node] : g_left[node];
            d++;
        }
        if (node < 0) {
            g_table[idx] = (uint16_t)((-node - 1) | (d << 8));
            g_table_node[idx] = -1;
        } else {
            g_table[idx] = (uint16_t)(0xff << 8);
            g_table_node[idx] = node;
        }
    }

    for (int i = 0; i < op_count; i++)
        g_len[i] = 0;
    std::vector<std::pair<int32_t, int>> stack;
    stack.push_back({g_root, 0});
    while (!stack.empty()) {
        auto [n, d] = stack.back();
        stack.pop_back();
        if (n < 0) {
            g_len[-n - 1] = d;
            continue;
        }
        stack.push_back({g_left[n], d + 1});
        stack.push_back({g_right[n], d + 1});
    }

    g_ready = true;
}

int code_length(int op) {
    return op >= 0 && op < op_count ? g_len[op] : 0;
}

static int read_op(bits_t& b) {
    uint32_t idx = bits::peek(b, g_table_bits);
    uint16_t e = g_table[idx];
    int len = e >> 8;
    if (len != 0xff) {
        bits::skip(b, len);
        return e & 0xff;
    }
    bits::skip(b, g_table_bits);
    int32_t node = g_table_node[idx];
    while (node >= 0 && !b.bad)
        node = bits::read_bit(b) ? g_right[node] : g_left[node];
    return node < 0 ? -node - 1 : op_count;
}

static bool down(fpath_t& fp) {
    if (fp.last + 1 >= 7)
        return false;
    fp.last++;
    return true;
}

static bool pop(fpath_t& fp, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        if (fp.last < 0)
            return false;
        fp.p[fp.last] = 0;
        fp.last--;
    }
    return fp.last >= 0;
}

static bool apply(int op, bits_t& b, fpath_t& fp) {
    int32_t* p = fp.p;
    int32_t& last = fp.last;
    switch (op) {
    case op_plus_one: p[last] += 1; return true;
    case op_plus_two: p[last] += 2; return true;
    case op_plus_three: p[last] += 3; return true;
    case op_plus_four: p[last] += 4; return true;
    case op_plus_n: p[last] += (int32_t)bits::read_ubitvar_fp(b) + 5; return true;

    case op_push_one_left_delta_zero_right_zero:
        if (!down(fp))
            return false;
        p[last] = 0;
        return true;
    case op_push_one_left_delta_zero_right_non_zero:
        if (!down(fp))
            return false;
        p[last] = (int32_t)bits::read_ubitvar_fp(b);
        return true;
    case op_push_one_left_delta_one_right_zero:
        p[last] += 1;
        if (!down(fp))
            return false;
        p[last] = 0;
        return true;
    case op_push_one_left_delta_one_right_non_zero:
        p[last] += 1;
        if (!down(fp))
            return false;
        p[last] = (int32_t)bits::read_ubitvar_fp(b);
        return true;
    case op_push_one_left_delta_n_right_zero:
        p[last] += (int32_t)bits::read_ubitvar_fp(b);
        if (!down(fp))
            return false;
        p[last] = 0;
        return true;
    case op_push_one_left_delta_n_right_non_zero:
        p[last] += (int32_t)bits::read_ubitvar_fp(b) + 2;
        if (!down(fp))
            return false;
        p[last] = (int32_t)bits::read_ubitvar_fp(b) + 1;
        return true;
    case op_push_one_left_delta_n_right_non_zero_pack6:
        p[last] += (int32_t)bits::read(b, 3) + 2;
        if (!down(fp))
            return false;
        p[last] = (int32_t)bits::read(b, 3) + 1;
        return true;
    case op_push_one_left_delta_n_right_non_zero_pack8:
        p[last] += (int32_t)bits::read(b, 4) + 2;
        if (!down(fp))
            return false;
        p[last] = (int32_t)bits::read(b, 4) + 1;
        return true;

    case op_push_two_left_delta_zero:
    case op_push_three_left_delta_zero: {
        int n = op == op_push_two_left_delta_zero ? 2 : 3;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read_ubitvar_fp(b);
        }
        return true;
    }
    case op_push_two_pack5_left_delta_zero:
    case op_push_three_pack5_left_delta_zero: {
        int n = op == op_push_two_pack5_left_delta_zero ? 2 : 3;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] = (int32_t)bits::read(b, 5);
        }
        return true;
    }
    case op_push_two_left_delta_one:
    case op_push_three_left_delta_one: {
        int n = op == op_push_two_left_delta_one ? 2 : 3;
        p[last] += 1;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read_ubitvar_fp(b);
        }
        return true;
    }
    case op_push_two_pack5_left_delta_one:
    case op_push_three_pack5_left_delta_one: {
        int n = op == op_push_two_pack5_left_delta_one ? 2 : 3;
        p[last] += 1;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read(b, 5);
        }
        return true;
    }
    case op_push_two_left_delta_n:
    case op_push_three_left_delta_n: {
        int n = op == op_push_two_left_delta_n ? 2 : 3;
        p[last] += (int32_t)bits::read_ubitvar(b) + 2;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read_ubitvar_fp(b);
        }
        return true;
    }
    case op_push_two_pack5_left_delta_n:
    case op_push_three_pack5_left_delta_n: {
        int n = op == op_push_two_pack5_left_delta_n ? 2 : 3;
        p[last] += (int32_t)bits::read_ubitvar(b) + 2;
        for (int i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read(b, 5);
        }
        return true;
    }

    case op_push_n: {
        int32_t n = (int32_t)bits::read_ubitvar(b);
        p[last] += (int32_t)bits::read_ubitvar(b);
        for (int32_t i = 0; i < n; i++) {
            if (!down(fp))
                return false;
            p[last] += (int32_t)bits::read_ubitvar_fp(b);
        }
        return true;
    }
    case op_push_n_and_non_topological: {
        for (int32_t i = 0; i <= last; i++)
            if (bits::read_bit(b))
                p[i] += bits::read_svarint(b) + 1;
        int32_t c = (int32_t)bits::read_ubitvar(b);
        for (int32_t i = 0; i < c; i++) {
            if (!down(fp))
                return false;
            p[last] = (int32_t)bits::read_ubitvar_fp(b);
        }
        return true;
    }

    case op_pop_one_plus_one:
        if (!pop(fp, 1))
            return false;
        p[last] += 1;
        return true;
    case op_pop_one_plus_n:
        if (!pop(fp, 1))
            return false;
        p[last] += (int32_t)bits::read_ubitvar_fp(b) + 1;
        return true;
    case op_pop_all_but_one_plus_one:
        if (!pop(fp, last))
            return false;
        p[0] += 1;
        return true;
    case op_pop_all_but_one_plus_n:
        if (!pop(fp, last))
            return false;
        p[0] += (int32_t)bits::read_ubitvar_fp(b) + 1;
        return true;
    case op_pop_all_but_one_plus_n_pack3:
        if (!pop(fp, last))
            return false;
        p[0] += (int32_t)bits::read(b, 3) + 1;
        return true;
    case op_pop_all_but_one_plus_n_pack6:
        if (!pop(fp, last))
            return false;
        p[0] += (int32_t)bits::read(b, 6) + 1;
        return true;
    case op_pop_n_plus_one:
        if (!pop(fp, (int32_t)bits::read_ubitvar_fp(b)))
            return false;
        p[last] += 1;
        return true;
    case op_pop_n_plus_n:
        if (!pop(fp, (int32_t)bits::read_ubitvar_fp(b)))
            return false;
        p[last] += bits::read_svarint(b);
        return true;
    case op_pop_n_and_non_topographical:
        if (!pop(fp, (int32_t)bits::read_ubitvar_fp(b)))
            return false;
        for (int32_t i = 0; i <= last; i++)
            if (bits::read_bit(b))
                p[i] += bits::read_svarint(b);
        return true;

    case op_non_topo_complex:
        for (int32_t i = 0; i <= last; i++)
            if (bits::read_bit(b))
                p[i] += bits::read_svarint(b);
        return true;
    case op_non_topo_penultimate_plus_one:
        if (last < 1)
            return false;
        p[last - 1] += 1;
        return true;
    case op_non_topo_complex_pack4:
        for (int32_t i = 0; i <= last; i++)
            if (bits::read_bit(b))
                p[i] += (int32_t)bits::read(b, 4) - 7;
        return true;
    }
    return false;
}

int read(bits_t& b, fpath_t* out, int cap, std::string& err) {
    fpath_t fp = {{-1, 0, 0, 0, 0, 0, 0}, 0};
    int n = 0;
    for (int guard = 0; guard < 4096; guard++) {
        int op = read_op(b);
        if (b.bad) {
            err = "bit stream ended inside a field path";
            return -1;
        }
        if (op == op_finish)
            return n;
        if (op >= op_count || !apply(op, b, fp)) {
            err = "field path op out of range";
            return -1;
        }
        if (b.bad) {
            err = "bit stream ended inside a field path";
            return -1;
        }
        if (fp.last < 0 || fp.last >= 7) {
            err = "field path too deep";
            return -1;
        }
        if (n >= cap) {
            err = "too many field paths";
            return -1;
        }
        out[n++] = fp;
    }
    err = "too many field path ops";
    return -1;
}

}
