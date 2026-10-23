#include "pch.h"

#include <wincodec.h>

#include "game/art.h"
#include "game/lz4.h"
#include "game/vpk.h"
#include "text.h"

namespace art {
    const char* SIZES[] = {"720p", "1080p", "360p"};
    const uint8_t PNG_MAGIC[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};

    bool valid_map(const std::string& map) {
        if (map.empty() || map.size() > 64)
            return false;
        for (char c : map)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
                return false;
        return true;
    }

    bool map_image(const std::string& csgo_dir, const std::string& map, const std::string& out_png, std::string& err) {
        if (!valid_map(map)) {
            err = "odd map name";
            return false;
        }
        std::string pak = csgo_dir + "\\pak01_dir.vpk";
        std::vector<uint8_t> tex;
        bool found = false;
        for (const char* size : SIZES) {
            std::string e;
            if (vpk::extract(pak, std::string("panorama/images/map_icons/screenshots/") + size + "/" + map + "_png.vtex_c", tex, e)) {
                found = true;
                break;
            }
            err = e;
        }
        if (!found)
            return false;
        uint32_t head = 0;
        if (tex.size() >= 4)
            memcpy(&head, tex.data(), 4);
        if (head + sizeof(PNG_MAGIC) > tex.size() || memcmp(tex.data() + head, PNG_MAGIC, sizeof(PNG_MAGIC))) {
            err = "texture has no png inside";
            return false;
        }
        FILE* f = _wfopen(text::wide(out_png).c_str(), L"wb");
        if (!f) {
            err = "cant write " + out_png;
            return false;
        }
        bool ok = fwrite(tex.data() + head, 1, tex.size() - head, f) == tex.size() - head;
        fclose(f);
        return ok;
    }

    const int BGRA8888 = 28;
    const int PNG_RGBA8888 = 16;
    const uint32_t COMPRESSED_MIPS = 4;

    uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
        uint32_t v = 0;
        if (at + 4 <= d.size())
            memcpy(&v, d.data() + at, 4);
        return v;
    }

    uint16_t u16(const std::vector<uint8_t>& d, size_t at) {
        uint16_t v = 0;
        if (at + 2 <= d.size())
            memcpy(&v, d.data() + at, 2);
        return v;
    }

    bool write_png(const std::string& path, const uint8_t* bgra, uint32_t w, uint32_t h, std::string& err) {
        IWICImagingFactory* wic = nullptr;
        IWICStream* stream = nullptr;
        IWICBitmapEncoder* enc = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
                  SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(text::wide(path).c_str(), GENERIC_WRITE)) &&
                  SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
                  SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h)) &&
                  SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == GUID_WICPixelFormat32bppBGRA &&
                  SUCCEEDED(frame->WritePixels(h, w * 4, w * h * 4, (BYTE*)bgra)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
        if (frame)
            frame->Release();
        if (enc)
            enc->Release();
        if (stream)
            stream->Release();
        if (wic)
            wic->Release();
        if (!ok)
            err = "png encode failed";
        return ok;
    }

    bool texture_png(const std::string& pak, const std::string& path, const std::string& out_png, std::string& err) {
        std::vector<uint8_t> tex;
        if (!vpk::extract(pak, path, tex, err))
            return false;
        uint32_t file_size = u32(tex, 0);
        size_t blocks = 8 + u32(tex, 8);
        uint32_t count = u32(tex, 12);
        size_t data = 0;
        for (uint32_t i = 0; i < count; i++) {
            size_t b = blocks + i * 12;
            if (b + 12 <= tex.size() && !memcmp(tex.data() + b, "DATA", 4))
                data = b + 4 + u32(tex, b + 4);
        }
        if (!data || data + 40 > tex.size()) {
            err = "texture has no data block";
            return false;
        }
        uint32_t w = u16(tex, data + 20), h = u16(tex, data + 22);
        int format = tex[data + 26], mips = tex[data + 27];
        if (format == PNG_RGBA8888) {
            if (file_size + sizeof(PNG_MAGIC) > tex.size() || memcmp(tex.data() + file_size, PNG_MAGIC, sizeof(PNG_MAGIC))) {
                err = "png texture has no png inside";
                return false;
            }
            FILE* f = _wfopen(text::wide(out_png).c_str(), L"wb");
            if (!f) {
                err = "cant write " + out_png;
                return false;
            }
            bool ok = fwrite(tex.data() + file_size, 1, tex.size() - file_size, f) == tex.size() - file_size;
            fclose(f);
            return ok;
        }
        if (format != BGRA8888 || mips < 1 || mips > 16 || !w || !h || w > 8192 || h > 8192) {
            err = "unsupported texture format " + std::to_string(format) + " with " + std::to_string(mips) + " mips";
            return false;
        }
        size_t extra = data + 32 + u32(tex, data + 32);
        uint32_t extras = u32(tex, data + 36);
        std::vector<uint32_t> packed;
        for (uint32_t i = 0; i < extras; i++) {
            size_t e = extra + i * 12;
            if (u32(tex, e) == COMPRESSED_MIPS) {
                size_t at = e + 4 + u32(tex, e + 4);
                uint32_t n = u32(tex, at + 8);
                for (uint32_t k = 0; k < n && k < 16; k++)
                    packed.push_back(u32(tex, at + 12 + k * 4));
            }
        }
        std::vector<uint8_t> pixels((size_t)w * h * 4);
        size_t skip = 0;
        if (!packed.empty()) {
            if (packed.size() != (size_t)mips) {
                err = "texture mip table does not match its mips";
                return false;
            }
            for (int m = 1; m < mips; m++)
                skip += packed[m];
            size_t len = 0;
            if (file_size + skip + packed[0] > tex.size() || !lz4::decompress(tex.data() + file_size + skip, packed[0], pixels.data(), pixels.size(), len) ||
                len != pixels.size()) {
                err = "texture did not unpack";
                return false;
            }
        } else {
            for (int m = 1; m < mips; m++)
                skip += (size_t)std::max(1u, w >> m) * std::max(1u, h >> m) * 4;
            if (file_size + skip + pixels.size() > tex.size()) {
                err = "texture is cut short";
                return false;
            }
            memcpy(pixels.data(), tex.data() + file_size + skip, pixels.size());
        }
        return write_png(out_png, pixels.data(), w, h, err);
    }

    bool radar_image(const std::string& csgo_dir, const std::string& name, const std::string& out_png, std::string& err) {
        if (!valid_map(name)) {
            err = "odd map name";
            return false;
        }
        return texture_png(csgo_dir + "\\pak01_dir.vpk","panorama/images/overheadmaps/" + name + "_radar_psd.vtex_c", out_png, err);
    }

    std::string kv(const std::string& text, const std::string& key, size_t from = 0, size_t to = std::string::npos) {
        size_t at = text.find("\"" + key + "\"", from);
        if (at == std::string::npos || at > to)
            return {};
        size_t a = text.find('"', at + key.size() + 2);
        size_t b = a == std::string::npos ? a : text.find('"', a + 1);
        if (b == std::string::npos)
            return {};
        return text.substr(a + 1, b - a - 1);
    }

    bool overview(const std::string& csgo_dir, const std::string& map, overview_t& out, std::string& err) {
        if (!valid_map(map)) {
            err = "odd map name";
            return false;
        }
        std::vector<uint8_t> raw;
        if (!vpk::extract(csgo_dir + "\\pak01_dir.vpk", "resource/overviews/" + map + ".txt", raw, err))
            return false;
        std::string text(raw.begin(), raw.end());
        out.pos_x = (float)atof(kv(text, "pos_x").c_str());
        out.pos_y = (float)atof(kv(text, "pos_y").c_str());
        out.scale = (float)atof(kv(text, "scale").c_str());
        size_t lower = text.find("\"lower\"");
        if (lower != std::string::npos) {
            out.lower = true;
            out.split_z = (float)atof(kv(text, "AltitudeMax", lower, lower + 200).c_str());
        }
        if (out.scale <= 0) {
            err = "overview has no scale";
            return false;
        }
        return true;
    }
}
