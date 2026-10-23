#include "pch.h"

#include "demos/skins.h"
#include "parser/parser.h"

namespace skins {
    const int EVERY = 128;
    const int ATTR_PAINT = 6;
    const int ATTR_SEED = 7;
    const int ATTR_WEAR = 8;
    const int ATTR_STATTRAK = 80;
    const int ATTR_STICKER = 113;
    const int FIRST_KNIFE = 500;

    void keys(item_keys_t& k, const std::string& item) {
        k.def = access::key((item + "m_iItemDefinitionIndex").c_str());
        k.high = access::key((item + "m_iItemIDHigh").c_str());
        k.low = access::key((item + "m_iItemIDLow").c_str());
        k.account = access::key((item + "m_iAccountID").c_str());
        k.tag = access::key((item + "m_szCustomName").c_str());
        k.attrs = access::key((item + "m_NetworkedDynamicAttributes.m_Attributes").c_str());
        k.attr_def = access::key((item + "m_NetworkedDynamicAttributes.m_Attributes.m_iAttributeDefinitionIndex").c_str());
        k.attr_raw = access::key((item + "m_NetworkedDynamicAttributes.m_Attributes.m_iRawValue32").c_str());
    }

    void init(skins_t& s) {
        keys(s.weapon, "m_AttributeManager.m_Item.");
        keys(s.gloves, "m_EconGloves.");
    }

    uint32_t bits_of(float f) {
        uint32_t u;
        memcpy(&u, &f, 4);
        return u;
    }

    bool read(parser_t& p, item_keys_t& k, const entity_t& e, skin_t& out) {
        const field_ref_t* d = access::ref(k.def, p.lay, p.ser, e);
        if (!d)
            return false;
        out.def = (int)access::get_int(e, *d);
        if (out.def <= 0)
            return false;
        const field_ref_t* hi = access::ref(k.high, p.lay, p.ser, e);
        const field_ref_t* lo = access::ref(k.low, p.lay, p.ser, e);
        if (hi && lo)
            out.id = ((uint64_t)(uint32_t)access::get_int(e, *hi) << 32) | (uint32_t)access::get_int(e, *lo);
        const field_ref_t* acc = access::ref(k.account, p.lay, p.ser, e);
        out.account = acc ? (uint32_t)access::get_int(e, *acc) : 0;
        const field_ref_t* tag = access::ref(k.tag, p.lay, p.ser, e);
        const std::string* name = tag ? access::get_str(e, *tag) : nullptr;
        if (name)
            out.tag = *name;
        const field_ref_t* list = access::ref(k.attrs, p.lay, p.ser, e);
        const field_ref_t* ad = access::ref(k.attr_def, p.lay, p.ser, e);
        const field_ref_t* ar = access::ref(k.attr_raw, p.lay, p.ser, e);
        uint32_t n = list ? access::count(e, *list) : 0;
        for (uint32_t i = 0; ad && ar && i < n && i < 64; i++) {
            int a = (int)access::get_int(e, *ad, (int)i);
            float v = access::get_float(e, *ar, (int)i);
            if (a == ATTR_PAINT)
                out.paint = (int)floorf(v);
            else if (a == ATTR_SEED)
                out.seed = (int)floorf(v);
            else if (a == ATTR_WEAR)
                out.wear = v;
            else if (a == ATTR_STATTRAK)
                out.stattrak = (int)bits_of(v);
            else if (a >= ATTR_STICKER && a < ATTR_STICKER + 20 && (a - ATTR_STICKER) % 4 == 0)
                out.stickers[(a - ATTR_STICKER) / 4] = bits_of(v);
        }
        return true;
    }

    void keep(skins_t& s, skin_t k) {
        if (!k.id || !k.account)
            return;
        auto t = s.teams.find(k.account);
        if (t != s.teams.end() && (t->second == 2 || t->second == 3))
            k.sides = (uint8_t)(t->second == 2 ? 1 : 2);
        auto it = s.items.find(k.id);
        if (it != s.items.end())
            k.sides |= it->second.sides;
        s.items[k.id] = k;
    }

    void on_frame(skins_t& s, parser_t& p) {
        if (!p.ready || p.tick < s.next)
            return;
        s.next = p.tick + EVERY;
        for (int i = 1; i <= 64; i++) {
            const entity_t* c = entities::get(p.ents, i);
            const field_ref_t* st = c ? access::ref(s.steam, p.lay, p.ser, *c) : nullptr;
            const field_ref_t* tm = st ? access::ref(s.team, p.lay, p.ser, *c) : nullptr;
            if (st && tm)
                s.teams[(uint32_t)access::get_u64(*c, *st)] = (int)access::get_int(*c, *tm);
        }
        for (size_t i = 0; i < p.ents.list.size(); i++) {
            const entity_t* e = entities::get(p.ents, (int)i);
            if (!e)
                continue;
            skin_t k;
            if (read(p, s.weapon, *e, k)) {
                if (!k.paint) {
                    const field_ref_t* fp = access::ref(s.fallback_paint, p.lay, p.ser, *e);
                    int paint = fp ? (int)access::get_int(*e, *fp) : 0;
                    if (paint > 0) {
                        const field_ref_t* fs = access::ref(s.fallback_seed, p.lay, p.ser, *e);
                        const field_ref_t* fw = access::ref(s.fallback_wear, p.lay, p.ser, *e);
                        const field_ref_t* ft = access::ref(s.fallback_stattrak, p.lay, p.ser, *e);
                        k.paint = paint;
                        k.seed = fs ? (int)access::get_int(*e, *fs) : 0;
                        k.wear = fw ? access::get_float(*e, *fw) : 0.f;
                        k.stattrak = ft ? (int)access::get_int(*e, *ft) : -1;
                    }
                }
                if (k.paint > 0 || k.def >= FIRST_KNIFE)
                    keep(s, k);
                continue;
            }
            skin_t g;
            if (read(p, s.gloves, *e, g)) {
                g.gloves = true;
                keep(s, g);
                continue;
            }
            const field_ref_t* m = access::ref(s.music, p.lay, p.ser, *e);
            const field_ref_t* st = m ? access::ref(s.steam, p.lay, p.ser, *e) : nullptr;
            if (m && st) {
                int music = (int)access::get_int(*e, *m);
                uint32_t account = (uint32_t)access::get_u64(*e, *st);
                if (music > 0 && account)
                    s.musics[account] = music;
            }
        }
    }

    void of(const skins_t& s, uint64_t steamid, std::vector<skin_t>& out, int& music) {
        uint32_t account = (uint32_t)steamid;
        for (const auto& [id, k] : s.items)
            if (k.account == account)
                out.push_back(k);
        auto it = s.musics.find(account);
        music = it == s.musics.end() ? 0 : it->second;
    }
}
