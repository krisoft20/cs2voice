#include "pch.h"

#include "video/hevc.h"

namespace hevc {
    struct rbsp_t {
        std::vector<uint8_t> b;
        size_t pos = 0;

        rbsp_t(const uint8_t* p, size_t n) {
            b.reserve(n);
            int zeros = 0;
            for (size_t i = 0; i < n; i++) {
                if (zeros >= 2 && p[i] == 3) {
                    zeros = 0;
                    continue;
                }
                b.push_back(p[i]);
                zeros = p[i] ? 0 : zeros + 1;
            }
        }

        uint32_t bit() {
            if (pos >= b.size() * 8)
                return 0;
            uint32_t v = (b[pos >> 3] >> (7 - (pos & 7))) & 1;
            pos++;
            return v;
        }

        uint32_t u(int n) {
            uint32_t v = 0;
            for (int i = 0; i < n; i++)
                v = (v << 1) | bit();
            return v;
        }

        uint32_t ue() {
            int zeros = 0;
            while (!bit() && zeros < 32)
                zeros++;
            return ((1u << zeros) - 1) + u(zeros);
        }

        int32_t se() {
            uint32_t k = ue();
            return k & 1 ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
        }
    };

    int nal_type(const uint8_t* nal) {
        return (nal[0] >> 1) & 0x3f;
    }

    bool vcl(int type) {
        return type <= 21 && (type <= 9 || type >= 16);
    }

    bool irap(int type) {
        return type >= 16 && type <= 23;
    }

    bool idr(int type) {
        return type == nal_idr_w_radl || type == nal_idr_n_lp;
    }

    int ceil_log2(int v) {
        int n = 0;
        while ((1 << n) < v)
            n++;
        return n;
    }

    void skip_ptl(rbsp_t& r, int max_sub) {
        r.u(32);
        r.u(32);
        r.u(32);
        bool prof[8] = {}, lev[8] = {};
        for (int i = 0; i < max_sub; i++) {
            prof[i] = r.bit();
            lev[i] = r.bit();
        }
        if (max_sub > 0)
            for (int i = max_sub; i < 8; i++)
                r.u(2);
        for (int i = 0; i < max_sub; i++) {
            if (prof[i]) {
                r.u(32);
                r.u(32);
                r.u(24);
            }
            if (lev[i])
                r.u(8);
        }
    }

    void skip_scaling(rbsp_t& r) {
        for (int size = 0; size < 4; size++)
            for (int m = 0; m < 6; m += size == 3 ? 3 : 1) {
                if (!r.bit()) {
                    r.ue();
                    continue;
                }
                int n = std::min(64, 1 << (4 + (size << 1)));
                if (size > 1)
                    r.se();
                for (int i = 0; i < n; i++)
                    r.se();
            }
    }

    void parse_rps(rbsp_t& r, int idx, const rps_t* sets, int num_sets, rps_t& out, int* ref_count) {
        int inter = idx != 0 ? r.bit() : 0;
        if (ref_count)
            *ref_count = 0;
        if (!inter) {
            out.neg = std::min((int)r.ue(), 16);
            out.pos = std::min((int)r.ue(), 16);
            int poc = 0;
            for (int i = 0; i < out.neg; i++) {
                poc -= (int)r.ue() + 1;
                out.d0[i] = poc;
                out.u0[i] = r.bit();
            }
            poc = 0;
            for (int i = 0; i < out.pos; i++) {
                poc += (int)r.ue() + 1;
                out.d1[i] = poc;
                out.u1[i] = r.bit();
            }
            return;
        }
        int delta_idx = idx == num_sets ? (int)r.ue() + 1 : 1;
        int sign = r.bit();
        int delta = (1 - 2 * sign) * ((int)r.ue() + 1);
        const rps_t& ref = sets[std::max(0, idx - delta_idx)];
        if (ref_count)
            *ref_count = ref.count();
        bool used[33] = {}, keep[33] = {};
        for (int j = 0; j <= ref.count(); j++) {
            used[j] = r.bit();
            keep[j] = used[j] ? true : r.bit() != 0;
        }
        int i = 0;
        for (int j = ref.pos - 1; j >= 0; j--) {
            int d = ref.d1[j] + delta;
            if (d < 0 && keep[ref.neg + j] && i < 16) {
                out.d0[i] = d;
                out.u0[i++] = used[ref.neg + j];
            }
        }
        if (delta < 0 && keep[ref.count()] && i < 16) {
            out.d0[i] = delta;
            out.u0[i++] = used[ref.count()];
        }
        for (int j = 0; j < ref.neg; j++) {
            int d = ref.d0[j] + delta;
            if (d < 0 && keep[j] && i < 16) {
                out.d0[i] = d;
                out.u0[i++] = used[j];
            }
        }
        out.neg = i;
        i = 0;
        for (int j = ref.neg - 1; j >= 0; j--) {
            int d = ref.d0[j] + delta;
            if (d > 0 && keep[j] && i < 16) {
                out.d1[i] = d;
                out.u1[i++] = used[j];
            }
        }
        if (delta > 0 && keep[ref.count()] && i < 16) {
            out.d1[i] = delta;
            out.u1[i++] = used[ref.count()];
        }
        for (int j = 0; j < ref.pos; j++) {
            int d = ref.d1[j] + delta;
            if (d > 0 && keep[ref.neg + j] && i < 16) {
                out.d1[i] = d;
                out.u1[i++] = used[ref.neg + j];
            }
        }
        out.pos = i;
    }

    void parse_sps(rbsp_t& r, params_t& ps) {
        r.u(4);
        int max_sub = r.u(3);
        r.u(1);
        skip_ptl(r, max_sub);
        sps_t& s = ps.sps[r.ue() & 15];
        s = sps_t{};
        s.chroma = r.ue();
        if (s.chroma == 3)
            s.separate = r.bit();
        s.width = r.ue();
        s.height = r.ue();
        if (r.bit()) {
            r.ue();
            r.ue();
            r.ue();
            r.ue();
        }
        s.depth_luma = r.ue() + 8;
        s.depth_chroma = r.ue() + 8;
        s.log2_poc = r.ue() + 4;
        int ordering = r.bit();
        for (int i = ordering ? 0 : max_sub; i <= max_sub; i++) {
            s.max_dec = r.ue() + 1;
            s.max_reorder = r.ue();
            r.ue();
        }
        s.log2_min_cb = r.ue() + 3;
        s.log2_diff_cb = r.ue();
        s.log2_min_tb = r.ue() + 2;
        s.log2_diff_tb = r.ue();
        s.depth_inter = r.ue();
        s.depth_intra = r.ue();
        s.scaling = r.bit();
        if (s.scaling && r.bit())
            skip_scaling(r);
        s.amp = r.bit();
        s.sao = r.bit();
        s.pcm = r.bit();
        if (s.pcm) {
            s.pcm_luma = r.u(4) + 1;
            s.pcm_chroma = r.u(4) + 1;
            s.log2_min_pcm = r.ue() + 3;
            s.log2_diff_pcm = r.ue();
            s.pcm_loop = r.bit();
        }
        s.num_rps = std::min((int)r.ue(), 64);
        for (int i = 0; i < s.num_rps; i++)
            parse_rps(r, i, s.rps, s.num_rps, s.rps[i], nullptr);
        s.lt_present = r.bit();
        if (s.lt_present) {
            s.num_lt_sps = r.ue();
            for (int i = 0; i < s.num_lt_sps; i++) {
                r.u(s.log2_poc);
                r.bit();
            }
        }
        s.tmvp = r.bit();
        s.strong_intra = r.bit();
        if (r.bit()) {
            if (r.bit() && r.u(8) == 255) {
                r.u(16);
                r.u(16);
            }
            if (r.bit())
                r.bit();
            if (r.bit()) {
                r.u(3);
                s.full_range = r.bit();
                if (r.bit()) {
                    r.u(8);
                    r.u(8);
                    s.matrix = r.u(8);
                }
            }
        }
        s.valid = s.width > 0 && s.height > 0;
    }

    void parse_pps(rbsp_t& r, params_t& ps) {
        pps_t& p = ps.pps[r.ue() & 63];
        p = pps_t{};
        p.sps = r.ue() & 15;
        p.dependent = r.bit();
        p.output_flag = r.bit();
        p.extra_bits = r.u(3);
        p.sign_hiding = r.bit();
        p.cabac_init = r.bit();
        p.l0 = r.ue();
        p.l1 = r.ue();
        p.init_qp = r.se();
        p.constrained_intra = r.bit();
        p.transform_skip = r.bit();
        p.cu_qp_delta = r.bit();
        if (p.cu_qp_delta)
            p.diff_cu_qp = r.ue();
        p.cb = r.se();
        p.cr = r.se();
        p.chroma_offsets = r.bit();
        p.wpred = r.bit();
        p.wbipred = r.bit();
        p.transquant = r.bit();
        p.tiles = r.bit();
        p.wpp = r.bit();
        if (p.tiles) {
            p.cols = std::min((int)r.ue(), 19);
            p.rows = std::min((int)r.ue(), 21);
            p.uniform = r.bit();
            if (!p.uniform) {
                for (int i = 0; i < p.cols; i++)
                    p.col_w[i] = r.ue();
                for (int i = 0; i < p.rows; i++)
                    p.row_h[i] = r.ue();
            }
            p.loop_tiles = r.bit();
        }
        p.loop_slices = r.bit();
        if (r.bit()) {
            p.deblock_override = r.bit();
            p.deblock_off = r.bit();
            if (!p.deblock_off) {
                p.beta = r.se();
                p.tc = r.se();
            }
        }
        p.scaling = r.bit();
        if (p.scaling)
            skip_scaling(r);
        p.lists_mod = r.bit();
        p.merge_level = r.ue();
        p.ext = r.bit();
        p.valid = true;
    }

    bool parameter(params_t& ps, const uint8_t* nal, size_t n) {
        int type = nal_type(nal);
        if (type != nal_sps && type != nal_pps)
            return type == nal_vps;
        rbsp_t r(nal + 2, n - 2);
        if (type == nal_sps)
            parse_sps(r, ps);
        else
            parse_pps(r, ps);
        return true;
    }

    bool slice(const params_t& ps, const uint8_t* nal, size_t n, slice_t& out, std::string& err) {
        out = slice_t{};
        out.type = nal_type(nal);
        out.tid = (nal[1] & 7) - 1;
        rbsp_t r(nal + 2, n - 2);
        out.first = r.bit();
        if (!out.first)
            return true;
        if (irap(out.type))
            r.bit();
        out.pps = r.ue() & 63;
        const pps_t& pps = ps.pps[out.pps];
        const sps_t& sps = ps.sps[pps.sps];
        if (!pps.valid || !sps.valid) {
            err = "slice without parameter sets";
            return false;
        }
        r.u(pps.extra_bits);
        out.slice_type = r.ue();
        if (pps.output_flag)
            r.bit();
        if (sps.separate)
            r.u(2);
        if (idr(out.type))
            return true;
        out.lsb = r.u(sps.log2_poc);
        out.sps_rps = r.bit();
        if (!out.sps_rps) {
            size_t at = r.pos;
            parse_rps(r, sps.num_rps, sps.rps, sps.num_rps, out.rps, &out.rps_ref);
            out.rps_bits = (int)(r.pos - at);
        } else if (sps.num_rps > 0) {
            int idx = sps.num_rps > 1 ? (int)r.u(ceil_log2(sps.num_rps)) : 0;
            out.rps = sps.rps[std::min(idx, sps.num_rps - 1)];
        }
        if (sps.lt_present) {
            err = "long term references are not supported";
            return false;
        }
        return true;
    }
}
