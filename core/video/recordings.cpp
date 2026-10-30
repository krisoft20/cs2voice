#include "pch.h"

#include "json/json.h"
#include "parser/proto/proto.h"
#include "video/recordings.h"

namespace recordings {
    const double JOIN_GAP = 5.0;
    const double EXTEND_GAP = 240.0;
    const double EXTEND_TAIL = 8.0;

    rec_video_t video(const proto_field_t& f) {
        rec_video_t v;
        proto_t r = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(r, g)) {
            switch (g.num) {
            case 1: v.folder = proto::str(g); break;
            case 2: v.start = g.val / 1000.0; break;
            case 3: v.duration = g.val / 1000.0; break;
            case 9: v.bytes = g.val; break;
            case 10: v.origin = g.val / 1000.0; break;
            }
        }
        return v;
    }

    rec_match_t phase(const proto_field_t& f) {
        rec_match_t m;
        proto_t r = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(r, g)) {
            if (g.num == 5) {
                m.duration = g.val / 1000.0;
            } else if (g.num == 8) {
                m.start = g.val / 1000.0;
            } else if (g.num == 6 || g.num == 9) {
                std::string a, b;
                proto_t t = proto::make(g.data, g.len);
                proto_field_t h;
                while (proto::next(t, h)) {
                    if (h.num == 1)
                        a = proto::str(h);
                    else if (h.num == 2)
                        b = proto::str(h);
                }
                if (g.num == 6 && b == "Mode")
                    m.mode = a;
                else if (g.num == 6 && b == "Map")
                    m.map = a;
                else if (g.num == 9 && a == "K/D/A")
                    m.kda = b;
                else if (g.num == 9 && a == "Score")
                    m.score = b;
            }
        }
        return m;
    }

    void join(std::vector<rec_match_t>& list) {
        std::sort(list.begin(), list.end(), [](const rec_match_t& a, const rec_match_t& b) { return a.start < b.start; });
        std::vector<rec_match_t> out;
        for (const rec_match_t& m : list) {
            if (!out.empty()) {
                rec_match_t& last = out.back();
                if (last.mode == m.mode && last.map == m.map && m.start - (last.start + last.duration) < JOIN_GAP) {
                    last.duration = m.start + m.duration - last.start;
                    if (!m.kda.empty())
                        last.kda = m.kda;
                    if (!m.score.empty())
                        last.score = m.score;
                    continue;
                }
            }
            out.push_back(m);
        }
        list.swap(out);
    }

    rec_event_t mark(const proto_field_t& f) {
        rec_event_t e;
        e.kind = "event";
        int type = 0;
        proto_t r = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(r, g)) {
            switch (g.num) {
            case 3: type = (int)g.val; break;
            case 5: e.id = (int)g.val; break;
            case 6: e.t = g.val / 1000.0; break;
            }
        }
        e.icon = type == 3 ? "cs2_multi_kill" : "cs2_gun_kill";
        e.title = type == 3 ? "Multi kill" : "Kill";
        return e;
    }

    void highlights(const proto_field_t& f, std::map<std::pair<std::string, int>, std::pair<std::string, std::string>>& out) {
        proto_t r = proto::make(f.data, f.len);
        proto_field_t g;
        while (proto::next(r, g)) {
            if (g.num != 2 || g.wire != 2)
                continue;
            std::string name, icon, title;
            int id = 0;
            proto_t q = proto::make(g.data, g.len);
            proto_field_t h;
            while (proto::next(q, h)) {
                switch (h.num) {
                case 4: name = proto::str(h); break;
                case 5: id = (int)h.val; break;
                case 8: icon = proto::str(h); break;
                case 9: title = proto::str(h); break;
                }
            }
            if (!name.empty() && !icon.empty())
                out[{name, id}] = {icon, title};
        }
    }

    bool index(const std::string& pb, std::vector<rec_timeline_t>& out) {
        proto_t r = proto::make((const uint8_t*)pb.data(), pb.size());
        proto_field_t f;
        std::map<std::pair<std::string, int>, std::pair<std::string, std::string>> named;
        while (proto::next(r, f)) {
            if (f.num == 2 && f.wire == 2)
                highlights(f, named);
            if (f.num != 1 || f.wire != 2)
                continue;
            rec_timeline_t t;
            proto_t q = proto::make(f.data, f.len);
            proto_field_t g;
            while (proto::next(q, g)) {
                switch (g.num) {
                case 1: t.name = proto::str(g); break;
                case 2: t.app = (int)g.val; break;
                case 3: t.unix = (int64_t)g.val; break;
                case 4: t.duration = g.val / 1000.0; break;
                case 5: t.videos.push_back(video(g)); break;
                case 6: t.matches.push_back(phase(g)); break;
                case 7: t.marks.push_back(mark(g)); break;
                }
            }
            join(t.matches);
            if (!t.name.empty())
                out.push_back(t);
        }
        for (rec_timeline_t& t : out)
            for (rec_event_t& e : t.marks) {
                auto it = named.find({t.name, e.id});
                if (it != named.end()) {
                    e.icon = it->second.first;
                    e.title = it->second.second;
                }
            }
        return !r.bad;
    }

    bool round_start(const rec_event_t& e) {
        return e.kind == "event" && e.icon.size() > 4 && e.icon.compare(0, 4, "cs2_") == 0 &&
               e.icon.find_first_not_of("0123456789", 4) == std::string::npos;
    }

    void extend(std::vector<rec_match_t>& list, const std::vector<rec_event_t>& events) {
        for (size_t i = 0; i < list.size(); i++) {
            rec_match_t& m = list[i];
            double end = m.start + m.duration;
            double limit = i + 1 < list.size() ? list[i + 1].start : 1e18;
            double last = -1;
            for (const rec_event_t& e : events)
                if (e.t > end && e.t < limit && round_start(e) && e.text == m.map)
                    last = e.t;
            if (last < 0)
                continue;
            double tail = last;
            for (const rec_event_t& e : events)
                if (e.kind == "event" && e.t > tail && e.t < limit && e.t - tail < EXTEND_GAP && !(round_start(e) && e.text != m.map))
                    tail = e.t;
            m.duration = tail + EXTEND_TAIL - m.start;
        }
    }

    bool events(const std::string& text, std::vector<rec_event_t>& out) {
        json_t j;
        std::string err;
        if (!json::parse(text, j, err))
            return false;
        const json_t* list = json::at(j, "entries");
        if (!list || list->kind != jk_arr)
            return false;
        for (const json_t& e : list->arr) {
            rec_event_t ev;
            ev.kind = json::str(json::at(e, "type"));
            if (ev.kind != "event" && ev.kind != "state_description" && ev.kind != "usermarker")
                continue;
            const json_t* at = json::at(e, "time");
            ev.t = (at && at->kind == jk_str ? atof(at->str.c_str()) : json::num(at)) / 1000.0;
            ev.icon = json::str(json::at(e, "icon"));
            ev.title = json::str(json::at(e, "title"));
            ev.text = json::str(json::at(e, "description"));
            out.push_back(ev);
        }
        std::stable_sort(out.begin(), out.end(), [](const rec_event_t& a, const rec_event_t& b) { return a.t < b.t; });
        return true;
    }
}
