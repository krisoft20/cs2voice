#pragma once

namespace hevc {
    enum nal_t {
        nal_rasl_n = 8,
        nal_rasl_r = 9,
        nal_bla_w_lp = 16,
        nal_idr_w_radl = 19,
        nal_idr_n_lp = 20,
        nal_cra = 21,
        nal_vps = 32,
        nal_sps = 33,
        nal_pps = 34,
    };

    struct rps_t {
        int neg = 0;
        int pos = 0;
        int d0[16] = {};
        int d1[16] = {};
        bool u0[16] = {};
        bool u1[16] = {};
        int count() const { return neg + pos; }
    };

    struct sps_t {
        bool valid = false;
        int chroma = 1, separate = 0, width = 0, height = 0, depth_luma = 8, depth_chroma = 8, log2_poc = 4;
        int max_dec = 1, max_reorder = 0;
        int log2_min_cb = 3, log2_diff_cb = 0, log2_min_tb = 2, log2_diff_tb = 0, depth_inter = 0, depth_intra = 0;
        int scaling = 0, amp = 0, sao = 0, pcm = 0, pcm_luma = 0, pcm_chroma = 0, log2_min_pcm = 0, log2_diff_pcm = 0, pcm_loop = 0;
        int num_rps = 0;
        rps_t rps[65];
        int lt_present = 0, num_lt_sps = 0;
        int tmvp = 0, strong_intra = 0;
        int full_range = 0, matrix = 2;
    };

    struct pps_t {
        bool valid = false;
        int sps = 0, dependent = 0, output_flag = 0, extra_bits = 0, sign_hiding = 0, cabac_init = 0;
        int l0 = 0, l1 = 0, init_qp = 0, constrained_intra = 0, transform_skip = 0, cu_qp_delta = 0, diff_cu_qp = 0;
        int cb = 0, cr = 0, chroma_offsets = 0, wpred = 0, wbipred = 0, transquant = 0, tiles = 0, wpp = 0;
        int cols = 0, rows = 0, uniform = 1, col_w[19] = {}, row_h[21] = {}, loop_tiles = 1, loop_slices = 0;
        int deblock_override = 0, deblock_off = 0, beta = 0, tc = 0, scaling = 0, lists_mod = 0, merge_level = 0, ext = 0;
    };

    struct params_t {
        sps_t sps[16];
        pps_t pps[64];
    };

    struct slice_t {
        int type = 0;
        int tid = 0;
        bool first = false;
        int pps = 0;
        int slice_type = 0;
        int lsb = 0;
        bool sps_rps = true;
        rps_t rps;
        int rps_bits = 0;
        int rps_ref = 0;
    };

    int nal_type(const uint8_t* nal);
    bool vcl(int type);
    bool irap(int type);
    bool idr(int type);
    bool parameter(params_t& ps, const uint8_t* nal, size_t n);
    bool slice(const params_t& ps, const uint8_t* nal, size_t n, slice_t& out, std::string& err);
}
