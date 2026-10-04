#include "scene.h"

#include "json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// binary PNM (P5 or P6), 8-bit: the header's tokens (comments allowed), then ONE whitespace byte, then the data
bool read_pnm(const std::string& path, const char* magic, int want_w, int want_h, std::string& data, size_t& offset,
              std::string& err) {
    const std::string name = path.substr(path.find_last_of('/') + 1);
    if (!read_file(path, data)) { err = name + ": missing or unreadable"; return false; }
    size_t i = 0;
    auto token = [&](std::string& tok) {
        for (;;) {
            while (i < data.size() && std::strchr(" \t\r\n", data[i])) ++i;
            if (i < data.size() && data[i] == '#') {
                while (i < data.size() && data[i] != '\n') ++i;
                continue;
            }
            break;
        }
        const size_t b = i;
        while (i < data.size() && !std::strchr(" \t\r\n", data[i])) ++i;
        tok = data.substr(b, i - b);
        return !tok.empty();
    };
    std::string m, w, h, mx;
    if (!token(m) || m != magic) { err = name + ": not a binary " + magic + " file"; return false; }
    if (!token(w) || !token(h) || !token(mx)) { err = name + ": a short header"; return false; }
    if (mx != "255") { err = name + ": maxval " + mx + ", not 255 (8-bit)"; return false; }
    if (std::atoi(w.c_str()) != want_w || std::atoi(h.c_str()) != want_h) {
        err = name + ": " + w + "x" + h + ", not the manifest's " + std::to_string(want_w) + "x" + std::to_string(want_h);
        return false;
    }
    ++i;   // the one whitespace byte after maxval
    const size_t need = size_t(want_w) * want_h * (magic[1] == '6' ? 3 : 1);
    if (data.size() < i + need) { err = name + ": " + std::to_string(data.size() - i) + " data bytes, not " + std::to_string(need); return false; }
    if (data.size() > i + need) { err = name + ": trailing bytes after the image"; return false; }
    offset = i;
    return true;
}

} // namespace

namespace {

bool is_key(const std::string& k) {
    if (k.empty() || !(k[0] >= 'a' && k[0] <= 'z')) return false;
    for (char c : k)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

// one scene's two files: the base map and the stacks, into runs per role and the stack list
bool scene_files(const std::string& dir, const Export& ex, const Json& js, Scene& sc, std::string& err) {
    const Json* name = js.get("name");
    const Json* base = js.get("base");
    const Json* cover = js.get("cover");
    if (!js.is_object() || !name || !name->is_string() || name->str.empty() || !base || !base->is_string() || !cover ||
        !cover->is_string() || js.obj.size() != 3) {
        err = "manifest.json: a scene is {\"name\", \"base\", \"cover\"}"; return false;
    }
    sc.name = name->str;
    const size_t n = size_t(ex.width) * ex.height, nr = ex.roles.size();
    std::string data;
    size_t off = 0;
    if (!read_pnm(dir + "/" + base->str, "P5", ex.width, ex.height, data, off, err)) return false;
    const auto* b = reinterpret_cast<const uint8_t*>(data.data() + off);
    for (size_t k = 0; k < n; ++k)
        if (b[k] >= nr) {
            err = base->str + ": the role " + std::to_string(b[k]) + " at x " + std::to_string(k % ex.width) + ", y " +
                  std::to_string(k / ex.width) + " (the manifest has " + std::to_string(nr) + ")";
            return false;
        }
    std::string cv;
    if (!read_file(dir + "/" + cover->str, cv)) { err = cover->str + ": missing or unreadable"; return false; }
    const auto* c = reinterpret_cast<const uint8_t*>(cv.data());
    if (cv.size() < 12 || cv.compare(0, 8, "WTCOVER1") != 0) { err = cover->str + ": not a cover file (WTCOVER1)"; return false; }
    const uint32_t count = le32(c + 8);
    size_t at = 12;
    std::vector<uint8_t> stacked(n, 0);
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 5 > cv.size()) { err = cover->str + ": short at stack " + std::to_string(i); return false; }
        Stack st;
        st.index = le32(c + at);
        st.n = c[at + 4];
        at += 5;
        if (st.index >= n || (i > 0 && st.index <= sc.stacks.back().index) || st.n == 0 || at + 2 * size_t(st.n) > cv.size()) {
            err = cover->str + ": stack " + std::to_string(i) + " is not an ascending pixel with its paints"; return false;
        }
        st.base = b[st.index];
        st.first = uint32_t(sc.layers.size());
        st.deps = ex.roles[st.base].deps;
        for (int k = 0; k < st.n; ++k, at += 2) {
            const Layer l{c[at], c[at + 1]};
            if (l.role >= nr || l.cov == 0 || l.cov == 255) {
                err = cover->str + ": stack " + std::to_string(i) + " paints role " + std::to_string(l.role) +
                      " at coverage " + std::to_string(l.cov) + " (a role of the manifest, coverage 1..254)";
                return false;
            }
            st.deps |= ex.roles[l.role].deps;
            sc.layers.push_back(l);
        }
        stacked[st.index] = 1;
        sc.stacks.push_back(st);
    }
    if (at != cv.size()) { err = cover->str + ": trailing bytes after the stacks"; return false; }
    // the solid pixels: each row's runs of one base role, the stacked pixels left out
    sc.solid.assign(nr, {});
    for (int y = 0; y < ex.height; ++y) {
        const size_t row = size_t(y) * ex.width;
        for (int x = 0; x < ex.width;) {
            const size_t k = row + x;
            if (stacked[k]) { ++x; continue; }
            int e = x + 1;
            while (e < ex.width && !stacked[row + e] && b[row + e] == b[k]) ++e;
            sc.solid[b[k]].push_back(Run{uint32_t(k), uint32_t(e - x)});
            x = e;
        }
    }
    return true;
}

// themes.json (scene.h): strictly the export's shape, the first error named
bool themes_file(const std::string& path, Export& ex, std::string& err) {
    std::string text;
    if (!read_file(path, text)) {
        err = "themes.json: missing or unreadable; an export written before the presets: export the theme again "
              "(render.py --export)";
        return false;
    }
    Json js;
    std::string jerr;
    if (!json_parse(text, js, jerr)) { err = "themes.json: " + jerr; return false; }
    const Json* src = js.is_object() ? js.get("source") : nullptr;
    const Json* ths = js.is_object() ? js.get("themes") : nullptr;
    if (!src || !src->is_string() || !ths || !ths->is_array() || js.obj.size() != 2) {
        err = "themes.json: not {\"source\", \"themes\": [...]}"; return false;
    }
    for (const Json& t : ths->arr) {
        const Json* key = t.get("key");
        const Json* name = t.get("name");
        const Json* ground = t.get("ground");
        const Json* cols = t.get("colours");
        Theme T;
        if (!t.is_object() || t.obj.size() != 4 || !key || !key->is_string() || key->str.empty() || !name ||
            !name->is_string() || name->str.empty() || !ground || !ground->is_string() ||
            !parse_hex(ground->str, T.ground) || !cols || !cols->is_array() || cols->arr.empty()) {
            err = "themes.json: theme " + std::to_string(ex.themes.size() + 1) +
                  " is not {\"key\", \"name\", \"ground\": \"#rrggbb\", \"colours\": [...]}";
            return false;
        }
        T.key = key->str;
        T.name = name->str;
        if (theme_of(ex, T.key) >= 0) { err = "themes.json: the theme " + T.key + " twice"; return false; }
        for (const Json& c : cols->arr) {
            const Json* hex = c.get("hex");
            const Json* names = c.get("names");
            ThemeColour tc;
            bool ok = c.is_object() && c.obj.size() == 2 && hex && hex->is_string() && parse_hex(hex->str, tc.rgb) &&
                      names && names->is_array() && !names->arr.empty();
            for (size_t k = 0; ok && k < names->arr.size(); ++k) {
                ok = names->arr[k].is_string() && !names->arr[k].str.empty();
                if (ok) tc.names.push_back(names->arr[k].str);
            }
            for (const ThemeColour& o : T.colours) ok = ok && !(o.rgb == tc.rgb);
            if (!ok) {
                err = "themes.json: " + T.key + "'s colour " + std::to_string(T.colours.size() + 1) +
                      " is not {\"hex\": a #rrggbb not listed before, \"names\": [one or more]}";
                return false;
            }
            T.colours.push_back(std::move(tc));
        }
        if (!(T.colours.front().rgb == T.ground)) { err = "themes.json: " + T.key + "'s first colour is not its ground"; return false; }
        ex.themes.push_back(std::move(T));
    }
    return true;
}

} // namespace

bool export_load(const std::string& dir, Export& ex, std::string& err) {
    ex = Export{};
    std::string text;
    if (!read_file(dir + "/manifest.json", text)) { err = "manifest.json: missing or unreadable (in " + dir + ")"; return false; }
    Json man;
    std::string jerr;
    if (!json_parse(text, man, jerr)) { err = "manifest.json: " + jerr; return false; }
    if (!man.is_object()) { err = "manifest.json: not an object"; return false; }
    if (man.get("layers") || man.get("background")) {
        err = "manifest.json: an export of the single-layer picker (layers, background); export the theme again "
              "(render.py --export)";
        return false;
    }
    const Json* w = man.get("width");
    const Json* h = man.get("height");
    const Json* act = man.get("active");
    const Json* els = man.get("elements");
    const Json* rls = man.get("roles");
    const Json* scs = man.get("scenes");
    if (!w || !w->is_number() || !h || !h->is_number() || w->num < 1 || h->num < 1) {
        err = "manifest.json: width and height must be positive numbers"; return false;
    }
    if (!act || !act->is_string()) { err = "manifest.json: active must name an element"; return false; }
    if (!els || !els->is_array() || els->arr.empty() || els->arr.size() > 32) { err = "manifest.json: elements must be a list of 1..32"; return false; }
    if (!rls || !rls->is_array() || rls->arr.empty() || rls->arr.size() > 255) { err = "manifest.json: roles must be a list of 1..255"; return false; }
    if (!scs || !scs->is_array() || scs->arr.empty()) { err = "manifest.json: scenes must be a non-empty list"; return false; }
    if (man.obj.size() != 6) { err = "manifest.json: not {width, height, active, elements, roles, scenes}"; return false; }
    ex.width = int(w->num);
    ex.height = int(h->num);

    // the scenes' names first: an element names its scene
    std::vector<std::string> scene_names;
    for (const Json& s : scs->arr) {
        const Json* nm = s.get("name");
        if (!s.is_object() || !nm || !nm->is_string()) { err = "manifest.json: every scene has a name"; return false; }
        for (const std::string& o : scene_names)
            if (o == nm->str) { err = "manifest.json: the scene " + o + " twice"; return false; }
        scene_names.push_back(nm->str);
    }
    for (const Json& e : els->arr) {
        const Json* key = e.get("key");
        const Json* name = e.get("name");
        const Json* col = e.get("colour");
        const Json* sc = e.get("scene");
        Element E;
        if (!e.is_object() || e.obj.size() != 4 || !key || !key->is_string() || !is_key(key->str) || !name ||
            !name->is_string() || name->str.empty() || !col || !col->is_string() || !parse_hex(col->str, E.colour) || !sc ||
            !sc->is_string()) {
            err = "manifest.json: an element is {\"key\": [a-z][a-z0-9_]*, \"name\", \"colour\": \"#rrggbb\", \"scene\"}"; return false;
        }
        E.key = key->str;
        E.name = name->str;
        for (const Element& o : ex.elements)
            if (o.key == E.key || o.name == E.name) { err = "manifest.json: two elements share the key or name " + E.key; return false; }
        for (size_t k = 0; k < scene_names.size(); ++k)
            if (scene_names[k] == sc->str) E.scene = int(k);
        if (E.scene < 0) { err = "manifest.json: element " + E.key + "'s scene " + sc->str + " is not among the scenes"; return false; }
        ex.elements.push_back(E);
    }
    ex.active = element_of(ex, act->str);
    if (ex.active < 0) { err = "manifest.json: the active element " + act->str + " is not among the elements"; return false; }

    auto elem = [&](const Json* j, int& out) {
        if (!j || !j->is_string()) return false;
        out = element_of(ex, j->str);
        return out >= 0;
    };
    for (const Json& r : rls->arr) {
        const Json* name = r.get("name");
        Role R;
        if (!r.is_object() || r.obj.size() != 2 || !name || !name->is_string() || name->str.empty()) {
            err = "manifest.json: a role is {\"name\", and one rule}"; return false;
        }
        R.name = name->str;
        const std::string where = "manifest.json: role " + R.name;
        if (const Json* e = r.get("element")) {
            R.kind = Role::Kind::Element;
            if (!elem(e, R.el)) { err = where + "'s element is not an element's key"; return false; }
            R.deps = 1u << R.el;
        } else if (const Json* c = r.get("colour")) {
            R.kind = Role::Kind::Colour;
            if (!c->is_string() || !parse_hex(c->str, R.rgb)) { err = where + "'s colour is not #rrggbb"; return false; }
        } else if (const Json* s = r.get("scale")) {
            R.kind = Role::Kind::Scale;
            const Json* num = s->get("num");
            const Json* den = s->get("den");
            if (!s->is_object() || s->obj.size() != 3 || !elem(s->get("of"), R.el) || !num || !num->is_number() || !den ||
                !den->is_number() || num->num < 0 || num->num > 65535 || den->num < 1 || den->num > 65535 ||
                num->num != std::floor(num->num) || den->num != std::floor(den->num)) {
                err = where + "'s scale is {\"of\": element, \"num\": 0..65535, \"den\": 1..65535}"; return false;
            }
            R.num = int(num->num);
            R.den = int(den->num);
            R.deps = 1u << R.el;
        } else if (const Json* d = r.get("derive")) {
            R.kind = Role::Kind::Derive;
            const Json* over = d->get("over");
            const Json* mix = d->get("linear_mix");
            if (!d->is_object() || d->obj.size() != 3 || !elem(d->get("from"), R.el) || !over || !over->is_string() || !mix ||
                !mix->is_number() || !(mix->num >= 0 && mix->num <= 1)) {
                err = where + "'s derive is {\"from\": element, \"over\": element or \"#rrggbb\", \"linear_mix\": 0..1}"; return false;
            }
            if (!parse_hex(over->str, R.rgb)) {
                R.over = element_of(ex, over->str);
                if (R.over < 0) { err = where + "'s derive.over is neither an element nor #rrggbb"; return false; }
            }
            R.linear_mix = mix->num;
            R.deps = (1u << R.el) | (R.over >= 0 ? 1u << R.over : 0u);
        } else {
            err = where + " has no rule (element, colour, scale, derive)"; return false;
        }
        ex.roles.push_back(R);
    }
    for (const Json& s : scs->arr) {
        Scene sc;
        if (!scene_files(dir, ex, s, sc, err)) return false;
        ex.scenes.push_back(std::move(sc));
    }
    return themes_file(dir + "/themes.json", ex, err);
}

int theme_of(const Export& ex, const std::string& key) {
    for (size_t k = 0; k < ex.themes.size(); ++k)
        if (ex.themes[k].key == key) return int(k);
    return -1;
}

int element_of(const Export& ex, const std::string& key) {
    for (size_t k = 0; k < ex.elements.size(); ++k)
        if (ex.elements[k].key == key) return int(k);
    return -1;
}

Rgb role_colour(const Export& ex, const Role& r) {
    switch (r.kind) {
        case Role::Kind::Element: return ex.elements[r.el].colour;
        case Role::Kind::Scale: return scale_rgb(ex.elements[r.el].colour, r.num, r.den);
        case Role::Kind::Derive: return lin_mix(ex.elements[r.el].colour, r.over >= 0 ? ex.elements[r.over].colour : r.rgb, r.linear_mix);
        default: return r.rgb;
    }
}

void role_words(const Export& ex, std::vector<uint32_t>& words) {
    words.resize(ex.roles.size());
    for (size_t k = 0; k < ex.roles.size(); ++k) words[k] = word_of(role_colour(ex, ex.roles[k]));
}

namespace {

inline uint32_t stack_word(const Scene& sc, const Stack& st, const std::vector<uint32_t>& words) {
    uint32_t d = words[st.base];
    const Layer* l = sc.layers.data() + st.first;
    for (int k = 0; k < st.n; ++k) d = over_n_8(words[l[k].role], l[k].cov, d);
    return d;
}

inline void fill_runs(const std::vector<Run>& runs, uint32_t w, uint32_t* picture) {
    for (const Run& r : runs) std::fill_n(picture + r.start, r.len, w);
}

} // namespace

void scene_paint(const Export& ex, int scene, const std::vector<uint32_t>& words, uint32_t* picture) {
    const Scene& sc = ex.scenes[size_t(scene)];
    for (size_t r = 0; r < sc.solid.size(); ++r) fill_runs(sc.solid[r], words[r], picture);
    for (const Stack& st : sc.stacks) picture[st.index] = stack_word(sc, st, words);
}

void scene_repaint(const Export& ex, int scene, const std::vector<uint32_t>& old_words, const std::vector<uint32_t>& words,
                   uint32_t changed, uint32_t* picture) {
    const Scene& sc = ex.scenes[size_t(scene)];
    for (size_t r = 0; r < sc.solid.size(); ++r)
        if (old_words[r] != words[r]) fill_runs(sc.solid[r], words[r], picture);
    if (!changed) return;
    for (const Stack& st : sc.stacks)
        if (st.deps & changed) picture[st.index] = stack_word(sc, st, words);
}

bool view_holds(const Pick& p) {
    for (int k = 0; k < 3; ++k)
        if (!(p.x[k] >= 0 && p.x[k] <= axis_max(p.model, k))) return false;
    const Unit u = unit_of_view(p.model, p.x);
    return unit_in_gamut(u) && rgb_of_unit(u) == p.rgb;
}

namespace {

// the ranges a view's message names
std::string view_ranges(Model m) {
    return m == Model::Lch ? "L 0..100, C 0..160, h 0..360" : m == Model::Hsl ? "h 0..360, s and l 0..1" : "h 0..360, s and v 0..1";
}

// one view number, the whole token: true and the double
bool read_view_number(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    char* end = nullptr;
    out = std::strtod(tok.c_str(), &end);
    return end == tok.c_str() + tok.size();
}

// a JSON [a, b, c] of numbers into the pick's view of model m
bool view_of_json(const Json& a, Model m, Pick& p) {
    if (!a.is_array() || a.arr.size() != 3 || !a.arr[0].is_number() || !a.arr[1].is_number() || !a.arr[2].is_number())
        return false;
    p.has_view = true;
    p.model = m;
    for (int k = 0; k < 3; ++k) p.x[k] = a.arr[size_t(k)].num;
    return true;
}

} // namespace

std::string view_number(double x) {
    char buf[40];
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, x);
        if (std::strtod(buf, nullptr) == x) break;
    }
    return buf;
}

bool state_load(const std::string& path, std::map<std::string, Pick>& out, std::map<std::string, int>& entries,
                std::string& active, std::string& theme, Model& model, std::string& err) {
    out.clear();
    entries.clear();
    active.clear();
    theme.clear();
    model = Model::Hsv;
    std::string text;
    if (!read_file(path, text)) return true;   // no close yet
    Json st;
    std::string jerr;
    if (!json_parse(text, st, jerr)) { err = "state.json: " + jerr; return false; }
    if (!st.is_object()) { err = "state.json: not an object"; return false; }
    const Json* cols = st.get("colours");
    if (!cols || !cols->is_object()) {   // the first build's file: the colours alone, flat
        for (const auto& kv : st.obj) {
            Pick p;
            if (!kv.second.is_string() || !parse_hex(kv.second.str, p.rgb)) { err = "state.json: " + kv.first + " is not #rrggbb"; return false; }
            out[kv.first] = p;
        }
        return true;
    }
    const Json* ent = st.get("entry");
    const Json* act = st.get("active");  // absent before the multi-element picker
    const Json* thm = st.get("theme");   // absent when no theme strip is open (and before the presets)
    const Json* mdl = st.get("model");   // absent before the model switch: HSV
    const Json* views[3];                // "hsv" absent in the second build's file; "hsl", "lch" before the model switch
    size_t present = 0;
    bool views_ok = true;
    for (Model m : kAllModels) {
        const Json* v = views[int(m)] = st.get(model_word(m));
        if (v) { ++present; views_ok = views_ok && v->is_object(); }
    }
    if (!ent || !ent->is_object() || !views_ok || (act && !act->is_string()) ||
        (thm && (!thm->is_string() || thm->str.empty())) || (mdl && (!mdl->is_string() || !model_of_word(mdl->str, model))) ||
        st.obj.size() != 2u + present + (act ? 1u : 0u) + (thm ? 1u : 0u) + (mdl ? 1u : 0u)) {
        err = "state.json: not {\"active\": key, \"model\": \"hsv\" | \"hsl\" | \"lch\", \"colours\": {...}, \"hsv\": {...}, "
              "\"hsl\": {...}, \"lch\": {...}, \"entry\": {...}, \"theme\": key}";
        return false;
    }
    if (act) active = act->str;
    if (thm) theme = thm->str;
    for (const auto& kv : cols->obj) {
        Pick p;
        if (!kv.second.is_string() || !parse_hex(kv.second.str, p.rgb)) { err = "state.json: colours." + kv.first + " is not #rrggbb"; return false; }
        out[kv.first] = p;
    }
    for (Model m : kAllModels)
        if (views[int(m)])
            for (const auto& kv : views[int(m)]->obj) {
                const std::string where = std::string("state.json: ") + model_word(m) + "." + kv.first;
                const auto c = out.find(kv.first);
                if (c == out.end() || c->second.has_view || !view_of_json(kv.second, m, c->second)) {
                    err = where + " is not [a, b, c] beside a colour, the element's one view"; return false;
                }
                if (!view_holds(c->second)) {
                    err = where + " is not a view giving " + hex_of(c->second.rgb) + " (" + view_ranges(m) + ")"; return false;
                }
            }
    for (const auto& kv : ent->obj) {
        const double n = kv.second.num;
        if (!kv.second.is_number() || n < 1 || n != std::floor(n) || n > 1e9) {
            err = "state.json: entry." + kv.first + " is not a whole number from 1"; return false;
        }
        entries[kv.first] = int(n);
    }
    return true;
}

bool picks_load(const std::string& path, std::map<std::string, std::vector<Pick>>& out, std::string& err) {
    out.clear();
    std::string text;
    if (!read_file(path, text)) return true;   // no commit yet
    size_t at = 0;
    for (int line = 1; at < text.size(); ++line) {
        const size_t nl = text.find('\n', at);
        const std::string where = "picks.txt: line " + std::to_string(line);
        if (nl == std::string::npos) { err = where + " has no newline"; return false; }
        const std::string l = text.substr(at, nl - at);
        at = nl + 1;
        // the words, single spaces: <time> <key...> #RRGGBB [<model> <a> <b> <c>]
        std::vector<std::string> w;
        for (size_t b = 0;;) {
            const size_t e = l.find(' ', b);
            w.push_back(l.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        Pick p;
        const bool view = w.size() >= 7 && model_of_word(w[w.size() - 4], p.model);
        const size_t hex_at = w.size() - (view ? 5 : 1);
        std::string name;
        for (size_t k = 1; k < hex_at && w.size() >= 3; ++k) name += (k > 1 ? " " : "") + w[k];
        if (w.size() < 3 || w[0].empty() || name.empty() || !parse_hex(w[hex_at], p.rgb) ||
            (view && !(read_view_number(w[w.size() - 3], p.x[0]) && read_view_number(w[w.size() - 2], p.x[1]) &&
                       read_view_number(w[w.size() - 1], p.x[2])))) {
            err = where + " is not \"<time> <key> #RRGGBB <hsv | hsl | lch> <a> <b> <c>\" (nor \"<time> <key> #RRGGBB\")"; return false;
        }
        p.has_view = view;
        if (view && !view_holds(p)) {
            err = where + "'s " + model_word(p.model) + " is not a view giving " + hex_of(p.rgb) + " (" + view_ranges(p.model) + ")";
            return false;
        }
        out[name].push_back(p);
    }
    return true;
}

bool presets_load(const std::string& path, std::vector<Preset>& out, std::string& err) {
    out.clear();
    std::string text;
    if (!read_file(path, text)) return true;   // no save yet
    Json js;
    std::string jerr;
    if (!json_parse(text, js, jerr)) { err = "presets.json: " + jerr; return false; }
    const Json* ps = js.is_object() ? js.get("presets") : nullptr;
    if (!ps || !ps->is_array() || js.obj.size() != 1) { err = "presets.json: not {\"presets\": [...]}"; return false; }
    for (const Json& p : ps->arr) {
        const std::string where = "presets.json: preset " + std::to_string(out.size() + 1);
        const Json* num = p.get("number");
        const Json* saved = p.get("saved");
        const Json* cols = p.get("colours");
        const Json* views[3];
        size_t present = 0, nviews = 0;
        bool views_ok = true;
        for (Model m : kAllModels) {
            const Json* v = views[int(m)] = p.get(model_word(m));
            if (v) {
                ++present;
                views_ok = views_ok && v->is_object();
                if (v->is_object()) nviews += v->obj.size();
            }
        }
        if (!p.is_object() || p.obj.size() != 3 + present || !num || !num->is_number() || num->num < 1 || num->num > 1e9 ||
            num->num != std::floor(num->num) || !saved || !saved->is_string() || saved->str.empty() || !cols ||
            !cols->is_object() || cols->obj.empty() || !views_ok || nviews != cols->obj.size()) {
            err = where + " is not {\"number\": a whole number from 1, \"saved\", \"colours\": {...}, \"hsv\" / \"hsl\" / "
                          "\"lch\": {...} over the colours}";
            return false;
        }
        Preset P;
        P.number = int(num->num);
        P.saved = saved->str;
        if (!out.empty() && P.number <= out.back().number) { err = where + "'s number is not above the one before it"; return false; }
        for (const auto& kv : cols->obj) {
            Pick pk;
            const Json* a = nullptr;
            Model am = Model::Hsv;
            int found = 0;
            for (Model m : kAllModels)
                if (const Json* v = views[int(m)] ? views[int(m)]->get(kv.first) : nullptr) { a = v; am = m; ++found; }
            if (!kv.second.is_string() || !parse_hex(kv.second.str, pk.rgb) || P.colours.count(kv.first) || found != 1 ||
                !view_of_json(*a, am, pk)) {
                err = where + ": " + kv.first + " is not a #rrggbb once, with its [a, b, c] in one of hsv, hsl, lch"; return false;
            }
            if (!view_holds(pk)) {
                err = where + ": " + model_word(am) + "." + kv.first + " is not a view giving " + hex_of(pk.rgb); return false;
            }
            P.colours[kv.first] = pk;
        }
        out.push_back(std::move(P));
    }
    return true;
}
