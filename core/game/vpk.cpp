#include "pch.h"

#include "game/vpk.h"
#include "text.h"

namespace vpk {
    const uint32_t MAGIC = 0x55aa1234u;
    const uint16_t IN_DIR = 0x7fff;

    bool read_at(FILE* f, uint64_t off, uint8_t* dst, size_t n) {
        return _fseeki64(f, (long long)off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
    }

    static FILE* open_tree(const std::string& path, uint32_t& header_size, std::vector<uint8_t>& tree, std::string& err) {
        FILE* f = _wfopen(text::wide(path).c_str(), L"rb");
        if (!f) {
            err = "cant open " + path;
            return nullptr;
        }
        uint32_t hdr[3] = {};
        if (fread(hdr, 4, 3, f) != 3 || hdr[0] != MAGIC) {
            fclose(f);
            err = "not a vpk archive: " + path;
            return nullptr;
        }
        header_size = hdr[1] == 2 ? 28u : 12u;
        tree.resize(hdr[2]);
        if (!read_at(f, header_size, tree.data(), tree.size())) {
            fclose(f);
            err = "truncated vpk directory";
            return nullptr;
        }
        return f;
    }

    static bool cstr(const std::vector<uint8_t>& tree, size_t& p, std::string& s) {
        const void* z = memchr(tree.data() + p, 0, tree.size() - p);
        if (!z)
            return false;
        size_t e = (size_t)((const uint8_t*)z - tree.data());
        s.assign((const char*)tree.data() + p, e - p);
        p = e + 1;
        return true;
    }

    bool list(const std::string& path, const std::string& prefix, std::vector<std::string>& out, std::string& err) {
        uint32_t header_size = 0;
        std::vector<uint8_t> tree;
        FILE* f = open_tree(path, header_size, tree, err);
        if (!f)
            return false;
        fclose(f);
        size_t p = 0;
        std::string ext, dir, name;
        while (p < tree.size() && cstr(tree, p, ext) && !ext.empty()) {
            while (cstr(tree, p, dir) && !dir.empty()) {
                bool want = (dir + "/").rfind(prefix, 0) == 0 || prefix.rfind(dir + "/", 0) == 0;
                while (cstr(tree, p, name) && !name.empty()) {
                    if (p + 18 > tree.size())
                        break;
                    uint16_t preload = 0;
                    memcpy(&preload, tree.data() + p + 4, 2);
                    p += 18 + preload;
                    std::string full = (dir == " " ? std::string() : dir + "/") + name + "." + ext;
                    if (want && full.rfind(prefix, 0) == 0)
                        out.push_back(full);
                }
            }
        }
        return true;
    }

    bool extract(const std::string& path, const std::string& want, std::vector<uint8_t>& out, std::string& err) {
        uint32_t header_size = 0;
        std::vector<uint8_t> tree;
        FILE* f = open_tree(path, header_size, tree, err);
        if (!f)
            return false;
        size_t p = 0;
        std::string ext, dir, name;
        while (p < tree.size() && cstr(tree, p, ext) && !ext.empty()) {
            while (cstr(tree, p, dir) && !dir.empty()) {
                while (cstr(tree, p, name) && !name.empty()) {
                    if (p + 18 > tree.size())
                        break;
                    uint16_t preload = 0, archive = 0;
                    uint32_t offset = 0, length = 0;
                    memcpy(&preload, tree.data() + p + 4, 2);
                    memcpy(&archive, tree.data() + p + 6, 2);
                    memcpy(&offset, tree.data() + p + 8, 4);
                    memcpy(&length, tree.data() + p + 12, 4);
                    size_t preload_at = p + 18;
                    p += 18 + preload;
                    if ((dir == " " ? std::string() : dir + "/") + name + "." + ext != want)
                        continue;
                    out.assign(tree.begin() + preload_at, tree.begin() + preload_at + preload);
                    size_t have = out.size();
                    out.resize(have + length);
                    bool ok;
                    if (archive == IN_DIR) {
                        ok = read_at(f, (uint64_t)header_size + tree.size() + offset, out.data() + have, length);
                        fclose(f);
                    } else {
                        fclose(f);
                        size_t cut = path.rfind("_dir.vpk");
                        char num[16];
                        snprintf(num, sizeof(num), "_%03u.vpk", archive);
                        FILE* a = cut == std::string::npos ? nullptr : _wfopen(text::wide(path.substr(0, cut) + num).c_str(), L"rb");
                        ok = a && read_at(a, offset, out.data() + have, length);
                        if (a)
                            fclose(a);
                    }
                    if (!ok)
                        err = "cant read " + want;
                    return ok;
                }
            }
        }
        fclose(f);
        err = "no " + want + " in " + path;
        return false;
    }
}
