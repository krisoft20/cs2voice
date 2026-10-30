#include "pch.h"

#include <dxva.h>

#include "dbg.h"
#include "player/decoder.h"
#include "video/hevc.h"

static const GUID HEVC_MAIN = {0x5b11d51b, 0x2f4c, 0x4452, {0xbc, 0xc3, 0x09, 0xf2, 0xa1, 0x16, 0x0c, 0xc0}};

struct video_pic_t {
    int poc = 0;
    int epoch = 0;
    bool ref = false;
    bool wait = false;
    bool held = false;
    int64_t pts = 0;
};

struct video_decoder_t {
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11VideoDevice* vdev = nullptr;
    ID3D11VideoContext* vctx = nullptr;
    ID3D11VideoDecoder* dec = nullptr;
    ID3D11Texture2D* tex = nullptr;
    ID3D11VideoDecoderOutputView* views[decoder::SURFACES] = {};
    video_pic_t dpb[decoder::SURFACES];
    hevc::params_t ps;
    int length_size = 4;
    int width = 0;
    int height = 0;
    int reorder = 0;
    int prev_lsb = 0;
    int prev_msb = 0;
    int epoch = 0;
    bool first = true;
    bool skip_rasl = false;
    bool draining = false;
    UINT feedback = 0;
    std::vector<uint8_t> bs;
    std::vector<DXVA_Slice_HEVC_Short> slices;
};

namespace decoder {
    const hevc::sps_t* any_sps(const video_decoder_t* d) {
        for (const hevc::sps_t& s : d->ps.sps)
            if (s.valid)
                return &s;
        return nullptr;
    }

    video_decoder_t* create(ID3D11Device* dev, const mp4::track_t& track, std::string& err) {
        video_decoder_t* d = new video_decoder_t();
        d->length_size = track.length_size;
        for (const std::vector<uint8_t>& nal : track.params)
            if (nal.size() > 2)
                hevc::parameter(d->ps, nal.data(), nal.size());
        const hevc::sps_t* sps = any_sps(d);
        if (!sps || sps->chroma != 1 || sps->depth_luma != 8 || sps->scaling) {
            err = sps ? "unsupported hevc stream" : "no hevc parameter sets";
            destroy(d);
            return nullptr;
        }
        d->width = sps->width;
        d->height = sps->height;
        d->reorder = sps->max_reorder;
        d->dev = dev;
        dev->AddRef();
        dev->GetImmediateContext(&d->ctx);
        if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&d->vdev))) || FAILED(d->ctx->QueryInterface(IID_PPV_ARGS(&d->vctx)))) {
            err = "no video device";
            destroy(d);
            return nullptr;
        }
        D3D11_VIDEO_DECODER_DESC desc = {HEVC_MAIN, (UINT)d->width, (UINT)d->height, DXGI_FORMAT_NV12};
        UINT count = 0;
        d->vdev->GetVideoDecoderConfigCount(&desc, &count);
        D3D11_VIDEO_DECODER_CONFIG cfg = {};
        bool found = false;
        for (UINT i = 0; i < count && !found; i++)
            found = SUCCEEDED(d->vdev->GetVideoDecoderConfig(&desc, i, &cfg)) && cfg.ConfigBitstreamRaw == 1;
        if (!found || FAILED(d->vdev->CreateVideoDecoder(&desc, &cfg, &d->dec))) {
            err = "this gpu has no hevc decoder";
            destroy(d);
            return nullptr;
        }
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = (UINT)((d->width + 63) & ~63);
        td.Height = (UINT)((d->height + 63) & ~63);
        td.MipLevels = 1;
        td.ArraySize = SURFACES;
        td.Format = DXGI_FORMAT_NV12;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_DECODER;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, &d->tex))) {
            err = "cant create video surfaces";
            destroy(d);
            return nullptr;
        }
        for (int i = 0; i < SURFACES; i++) {
            D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC vd = {};
            vd.DecodeProfile = HEVC_MAIN;
            vd.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
            vd.Texture2D.ArraySlice = (UINT)i;
            if (FAILED(d->vdev->CreateVideoDecoderOutputView(d->tex, &vd, &d->views[i]))) {
                err = "cant create video views";
                destroy(d);
                return nullptr;
            }
        }
        dbg::print("[decoder] hevc %dx%d, reorder %d", d->width, d->height, d->reorder);
        return d;
    }

    void destroy(video_decoder_t* d) {
        if (!d)
            return;
        for (ID3D11VideoDecoderOutputView* v : d->views)
            if (v)
                v->Release();
        IUnknown* all[] = {d->tex, d->dec, d->vctx, d->vdev, d->ctx, d->dev};
        for (IUnknown* u : all)
            if (u)
                u->Release();
        delete d;
    }

    void fill(video_decoder_t* d, const hevc::sps_t& sps, const hevc::pps_t& pps, const hevc::slice_t& sl, int surf, int poc, DXVA_PicParams_HEVC& pp) {
        bool irap = hevc::irap(sl.type), idr = hevc::idr(sl.type);
        int min_cb = 1 << sps.log2_min_cb;
        pp.PicWidthInMinCbsY = (USHORT)((sps.width + min_cb - 1) / min_cb);
        pp.PicHeightInMinCbsY = (USHORT)((sps.height + min_cb - 1) / min_cb);
        pp.chroma_format_idc = sps.chroma;
        pp.separate_colour_plane_flag = sps.separate;
        pp.bit_depth_luma_minus8 = sps.depth_luma - 8;
        pp.bit_depth_chroma_minus8 = sps.depth_chroma - 8;
        pp.log2_max_pic_order_cnt_lsb_minus4 = sps.log2_poc - 4;
        pp.CurrPic.Index7Bits = surf;
        pp.sps_max_dec_pic_buffering_minus1 = (UCHAR)(sps.max_dec - 1);
        pp.log2_min_luma_coding_block_size_minus3 = (UCHAR)(sps.log2_min_cb - 3);
        pp.log2_diff_max_min_luma_coding_block_size = (UCHAR)sps.log2_diff_cb;
        pp.log2_min_transform_block_size_minus2 = (UCHAR)(sps.log2_min_tb - 2);
        pp.log2_diff_max_min_transform_block_size = (UCHAR)sps.log2_diff_tb;
        pp.max_transform_hierarchy_depth_inter = (UCHAR)sps.depth_inter;
        pp.max_transform_hierarchy_depth_intra = (UCHAR)sps.depth_intra;
        pp.num_short_term_ref_pic_sets = (UCHAR)sps.num_rps;
        pp.num_long_term_ref_pics_sps = (UCHAR)sps.num_lt_sps;
        pp.num_ref_idx_l0_default_active_minus1 = (UCHAR)pps.l0;
        pp.num_ref_idx_l1_default_active_minus1 = (UCHAR)pps.l1;
        pp.init_qp_minus26 = (CHAR)pps.init_qp;
        if (!idr && !sl.sps_rps) {
            pp.ucNumDeltaPocsOfRefRpsIdx = (UCHAR)sl.rps_ref;
            pp.wNumBitsForShortTermRPSInSlice = (USHORT)sl.rps_bits;
        }
        pp.scaling_list_enabled_flag = sps.scaling;
        pp.amp_enabled_flag = sps.amp;
        pp.sample_adaptive_offset_enabled_flag = sps.sao;
        pp.pcm_enabled_flag = sps.pcm;
        if (sps.pcm) {
            pp.pcm_sample_bit_depth_luma_minus1 = sps.pcm_luma - 1;
            pp.pcm_sample_bit_depth_chroma_minus1 = sps.pcm_chroma - 1;
            pp.log2_min_pcm_luma_coding_block_size_minus3 = sps.log2_min_pcm - 3;
            pp.log2_diff_max_min_pcm_luma_coding_block_size = sps.log2_diff_pcm;
            pp.pcm_loop_filter_disabled_flag = sps.pcm_loop;
        }
        pp.long_term_ref_pics_present_flag = sps.lt_present;
        pp.sps_temporal_mvp_enabled_flag = sps.tmvp;
        pp.strong_intra_smoothing_enabled_flag = sps.strong_intra;
        pp.dependent_slice_segments_enabled_flag = pps.dependent;
        pp.output_flag_present_flag = pps.output_flag;
        pp.num_extra_slice_header_bits = pps.extra_bits;
        pp.sign_data_hiding_enabled_flag = pps.sign_hiding;
        pp.cabac_init_present_flag = pps.cabac_init;
        pp.constrained_intra_pred_flag = pps.constrained_intra;
        pp.transform_skip_enabled_flag = pps.transform_skip;
        pp.cu_qp_delta_enabled_flag = pps.cu_qp_delta;
        pp.pps_slice_chroma_qp_offsets_present_flag = pps.chroma_offsets;
        pp.weighted_pred_flag = pps.wpred;
        pp.weighted_bipred_flag = pps.wbipred;
        pp.transquant_bypass_enabled_flag = pps.transquant;
        pp.tiles_enabled_flag = pps.tiles;
        pp.entropy_coding_sync_enabled_flag = pps.wpp;
        pp.uniform_spacing_flag = pps.uniform;
        pp.loop_filter_across_tiles_enabled_flag = pps.tiles ? pps.loop_tiles : 0;
        pp.pps_loop_filter_across_slices_enabled_flag = pps.loop_slices;
        pp.deblocking_filter_override_enabled_flag = pps.deblock_override;
        pp.pps_deblocking_filter_disabled_flag = pps.deblock_off;
        pp.lists_modification_present_flag = pps.lists_mod;
        pp.slice_segment_header_extension_present_flag = pps.ext;
        pp.IrapPicFlag = irap;
        pp.IdrPicFlag = idr;
        pp.IntraPicFlag = irap;
        pp.pps_cb_qp_offset = (CHAR)pps.cb;
        pp.pps_cr_qp_offset = (CHAR)pps.cr;
        if (pps.tiles) {
            pp.num_tile_columns_minus1 = (UCHAR)pps.cols;
            pp.num_tile_rows_minus1 = (UCHAR)pps.rows;
            for (int i = 0; i < 19; i++)
                pp.column_width_minus1[i] = (USHORT)pps.col_w[i];
            for (int i = 0; i < 21; i++)
                pp.row_height_minus1[i] = (USHORT)pps.row_h[i];
        }
        pp.diff_cu_qp_delta_depth = (UCHAR)pps.diff_cu_qp;
        pp.pps_beta_offset_div2 = (CHAR)pps.beta;
        pp.pps_tc_offset_div2 = (CHAR)pps.tc;
        pp.log2_parallel_merge_level_minus2 = (UCHAR)pps.merge_level;
        pp.CurrPicOrderCntVal = poc;
        pp.StatusReportFeedbackNumber = ++d->feedback;
    }

    void references(video_decoder_t* d, int surf, const std::vector<int>& before, const std::vector<int>& after, DXVA_PicParams_HEVC& pp) {
        for (int i = 0; i < 15; i++)
            pp.RefPicList[i].bPicEntry = 0xff;
        memset(pp.RefPicSetStCurrBefore, 0xff, 8);
        memset(pp.RefPicSetStCurrAfter, 0xff, 8);
        memset(pp.RefPicSetLtCurr, 0xff, 8);
        int n = 0;
        for (int i = 0; i < SURFACES && n < 15; i++) {
            if (!d->dpb[i].ref || i == surf)
                continue;
            pp.RefPicList[n].Index7Bits = i;
            pp.PicOrderCntValList[n] = d->dpb[i].poc;
            n++;
        }
        auto index_of = [&](int poc) {
            for (int i = 0; i < n; i++)
                if (pp.PicOrderCntValList[i] == poc)
                    return (UCHAR)i;
            return (UCHAR)0xff;
        };
        for (size_t i = 0; i < before.size() && i < 8; i++)
            pp.RefPicSetStCurrBefore[i] = index_of(before[i]);
        for (size_t i = 0; i < after.size() && i < 8; i++)
            pp.RefPicSetStCurrAfter[i] = index_of(after[i]);
    }

    bool submit(video_decoder_t* d, int surf, const DXVA_PicParams_HEVC& pp, std::string& err) {
        HRESULT hr;
        int tries = 0;
        while ((hr = d->vctx->DecoderBeginFrame(d->dec, d->views[surf], 0, nullptr)) == E_PENDING && tries++ < 1000)
            Sleep(1);
        if (FAILED(hr)) {
            err = "decoder begin frame failed";
            return false;
        }
        size_t pad = (128 - d->bs.size() % 128) % 128;
        d->bs.resize(d->bs.size() + pad, 0);
        d->slices.back().SliceBytesInBuffer += (UINT)pad;
        auto put = [&](D3D11_VIDEO_DECODER_BUFFER_TYPE type, const void* src, UINT n) {
            void* p = nullptr;
            UINT size = 0;
            if (FAILED(d->vctx->GetDecoderBuffer(d->dec, type, &size, &p)))
                return false;
            bool fits = size >= n;
            if (fits)
                memcpy(p, src, n);
            d->vctx->ReleaseDecoderBuffer(d->dec, type);
            return fits;
        };
        UINT slices = (UINT)(d->slices.size() * sizeof(DXVA_Slice_HEVC_Short));
        bool ok = put(D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS, &pp, sizeof(pp)) &&
                  put(D3D11_VIDEO_DECODER_BUFFER_BITSTREAM, d->bs.data(), (UINT)d->bs.size()) &&
                  put(D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL, d->slices.data(), slices);
        if (ok) {
            D3D11_VIDEO_DECODER_BUFFER_DESC bd[3] = {};
            bd[0].BufferType = D3D11_VIDEO_DECODER_BUFFER_PICTURE_PARAMETERS;
            bd[0].DataSize = sizeof(pp);
            bd[1].BufferType = D3D11_VIDEO_DECODER_BUFFER_BITSTREAM;
            bd[1].DataSize = (UINT)d->bs.size();
            bd[2].BufferType = D3D11_VIDEO_DECODER_BUFFER_SLICE_CONTROL;
            bd[2].DataSize = slices;
            ok = SUCCEEDED(d->vctx->SubmitDecoderBuffers(d->dec, 3, bd));
        }
        d->vctx->DecoderEndFrame(d->dec);
        if (!ok)
            err = "decoder rejected the frame";
        return ok;
    }

    bool decode(video_decoder_t* d, const uint8_t* au, size_t n, int64_t pts, std::string& err) {
        d->bs.clear();
        d->slices.clear();
        DXVA_PicParams_HEVC pp = {};
        int surf = -1;
        const uint8_t* p = au;
        const uint8_t* end = au + n;
        while (end - p > d->length_size) {
            uint32_t len = 0;
            for (int i = 0; i < d->length_size; i++)
                len = len << 8 | p[i];
            p += d->length_size;
            if (len > (uint32_t)(end - p) || len < 3) {
                err = "broken access unit";
                return false;
            }
            const uint8_t* nal = p;
            p += len;
            int type = hevc::nal_type(nal);
            if (hevc::parameter(d->ps, nal, len) || !hevc::vcl(type))
                continue;
            if ((type == hevc::nal_rasl_n || type == hevc::nal_rasl_r) && d->skip_rasl)
                return true;
            hevc::slice_t sl;
            if (!hevc::slice(d->ps, nal, len, sl, err))
                return false;
            if (sl.first && surf < 0) {
                const hevc::pps_t& pps = d->ps.pps[sl.pps];
                const hevc::sps_t& sps = d->ps.sps[pps.sps];
                if (sps.width != d->width || sps.height != d->height) {
                    err = "video size changed";
                    return false;
                }
                bool irap = hevc::irap(type), idr = hevc::idr(type);
                bool no_rasl = irap && (idr || type < hevc::nal_cra || d->first);
                if (irap) {
                    d->skip_rasl = no_rasl;
                    if (no_rasl)
                        d->epoch++;
                }
                d->first = false;
                int max_lsb = 1 << sps.log2_poc;
                int msb = 0;
                if (!(irap && no_rasl)) {
                    if (sl.lsb < d->prev_lsb && d->prev_lsb - sl.lsb >= max_lsb / 2)
                        msb = d->prev_msb + max_lsb;
                    else if (sl.lsb > d->prev_lsb && sl.lsb - d->prev_lsb > max_lsb / 2)
                        msb = d->prev_msb - max_lsb;
                    else
                        msb = d->prev_msb;
                }
                int poc = msb + sl.lsb;
                bool sub_nonref = type <= 14 && type % 2 == 0;
                if (sl.tid == 0 && !(type >= 6 && type <= 9) && !sub_nonref) {
                    d->prev_lsb = sl.lsb;
                    d->prev_msb = msb;
                }
                std::vector<int> before, after, keep;
                if (!idr) {
                    for (int i = 0; i < sl.rps.neg; i++)
                        (sl.rps.u0[i] ? before : keep).push_back(poc + sl.rps.d0[i]);
                    for (int i = 0; i < sl.rps.pos; i++)
                        (sl.rps.u1[i] ? after : keep).push_back(poc + sl.rps.d1[i]);
                }
                for (video_pic_t& pc : d->dpb) {
                    if (!pc.ref)
                        continue;
                    bool used = pc.epoch == d->epoch &&
                                (std::find(before.begin(), before.end(), pc.poc) != before.end() ||
                                 std::find(after.begin(), after.end(), pc.poc) != after.end() || std::find(keep.begin(), keep.end(), pc.poc) != keep.end());
                    if (!used)
                        pc.ref = false;
                }
                for (int i = 0; i < SURFACES && surf < 0; i++)
                    if (!d->dpb[i].ref && !d->dpb[i].wait && !d->dpb[i].held)
                        surf = i;
                if (surf < 0) {
                    err = "no free video surface";
                    return false;
                }
                fill(d, sps, pps, sl, surf, poc, pp);
                references(d, surf, before, after, pp);
                d->dpb[surf] = video_pic_t{poc, d->epoch, false, false, false, pts};
            }
            if (surf < 0)
                continue;
            DXVA_Slice_HEVC_Short s = {};
            s.BSNALunitDataLocation = (UINT)d->bs.size();
            s.SliceBytesInBuffer = len + 3;
            d->bs.push_back(0);
            d->bs.push_back(0);
            d->bs.push_back(1);
            d->bs.insert(d->bs.end(), nal, nal + len);
            d->slices.push_back(s);
        }
        if (surf < 0 || d->slices.empty())
            return true;
        if (!submit(d, surf, pp, err))
            return false;
        d->dpb[surf].ref = true;
        d->dpb[surf].wait = true;
        return true;
    }

    bool pop(video_decoder_t* d, frame_t& out) {
        int best = -1, waiting = 0;
        for (int i = 0; i < SURFACES; i++) {
            const video_pic_t& p = d->dpb[i];
            if (!p.wait)
                continue;
            waiting++;
            if (best < 0 || p.epoch < d->dpb[best].epoch || (p.epoch == d->dpb[best].epoch && p.poc < d->dpb[best].poc))
                best = i;
        }
        if (best < 0 || !(d->draining || waiting > d->reorder || d->dpb[best].epoch < d->epoch))
            return false;
        d->dpb[best].wait = false;
        d->dpb[best].held = true;
        out.surface = best;
        out.pts = d->dpb[best].pts;
        return true;
    }

    void drain(video_decoder_t* d) {
        d->draining = true;
    }

    void reset(video_decoder_t* d) {
        for (video_pic_t& p : d->dpb)
            p = video_pic_t{};
        d->prev_lsb = d->prev_msb = 0;
        d->first = true;
        d->skip_rasl = false;
        d->draining = false;
    }

    void release(video_decoder_t* d, int surface) {
        if (surface >= 0 && surface < SURFACES)
            d->dpb[surface].held = false;
    }

    ID3D11Texture2D* texture(video_decoder_t* d) {
        return d->tex;
    }

    int width(video_decoder_t* d) {
        return d->width;
    }

    int height(video_decoder_t* d) {
        return d->height;
    }

    bool full_range(video_decoder_t* d) {
        const hevc::sps_t* s = any_sps(d);
        return s && s->full_range;
    }

    bool bt601(video_decoder_t* d) {
        const hevc::sps_t* s = any_sps(d);
        return s && (s->matrix == 5 || s->matrix == 6);
    }
}
