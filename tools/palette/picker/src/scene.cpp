#include "scene.h"

#include "json.h"

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

bool scene_load(const std::string& dir, Scene& scene, std::string& err) {
    scene = Scene{};
    std::string text;
    if (!read_file(dir + "/manifest.json", text)) { err = "manifest.json: missing or unreadable (in " + dir + ")"; return false; }
    Json man;
    std::string jerr;
    if (!json_parse(text, man, jerr)) { err = "manifest.json: " + jerr; return false; }
    if (!man.is_object()) { err = "manifest.json: not an object"; return false; }
    const Json* w = man.get("width");
    const Json* h = man.get("height");
    const Json* bg = man.get("background");
    const Json* act = man.get("active");
    const Json* ls = man.get("layers");
    if (!w || !w->is_number() || !h || !h->is_number() || w->num < 1 || h->num < 1) {
        err = "manifest.json: width and height must be positive numbers"; return false;
    }
    if (!bg || !bg->is_string()) { err = "manifest.json: background must name a file"; return false; }
    if (!act || !act->is_string()) { err = "manifest.json: active must name a layer"; return false; }
    if (!ls || !ls->is_array() || ls->arr.empty()) { err = "manifest.json: layers must be a non-empty list"; return false; }
    scene.width = int(w->num);
    scene.height = int(h->num);
    const size_t n = size_t(scene.width) * scene.height;

    std::string data;
    size_t off = 0;
    if (!read_pnm(dir + "/" + bg->str, "P6", scene.width, scene.height, data, off, err)) return false;
    scene.background.resize(n);
    const auto* p = reinterpret_cast<const uint8_t*>(data.data() + off);
    for (size_t k = 0; k < n; ++k) scene.background[k] = word_of(Rgb{p[3 * k], p[3 * k + 1], p[3 * k + 2]});

    // the layers: names first, so a derive may follow a layer listed before or after it
    for (const Json& l : ls->arr) {
        const Json* name = l.get("name");
        if (!l.is_object() || !name || !name->is_string() || name->str.empty()) {
            err = "manifest.json: every layer is an object with a name"; return false;
        }
        for (const Layer& o : scene.layers)
            if (o.name == name->str) { err = "manifest.json: the layer name " + name->str + " twice"; return false; }
        Layer L;
        L.name = name->str;
        scene.layers.push_back(L);
    }
    for (size_t li = 0; li < ls->arr.size(); ++li) {
        const Json& l = ls->arr[li];
        Layer& L = scene.layers[li];
        const Json* mask = l.get("mask");
        const Json* col = l.get("colour");
        const Json* der = l.get("derive");
        if (!mask || !mask->is_string()) { err = "manifest.json: layer " + L.name + " names no mask"; return false; }
        L.mask_file = mask->str;
        if ((col != nullptr) == (der != nullptr)) {
            err = "manifest.json: layer " + L.name + " needs exactly one of colour and derive"; return false;
        }
        if (col) {
            if (!col->is_string() || !parse_hex(col->str, L.colour)) {
                err = "manifest.json: layer " + L.name + "'s colour is not #rrggbb"; return false;
            }
        } else {
            const Json* from = der->get("from");
            const Json* over = der->get("over");
            const Json* mix = der->get("linear_mix");
            if (!der->is_object() || !from || !from->is_string() || !over || !over->is_string() || !mix || !mix->is_number()) {
                err = "manifest.json: layer " + L.name + "'s derive is {\"from\": layer, \"over\": \"#rrggbb\", \"linear_mix\": t}";
                return false;
            }
            L.derived = true;
            for (size_t k = 0; k < scene.layers.size(); ++k)
                if (scene.layers[k].name == from->str) L.from = int(k);
            if (L.from < 0 || L.from == int(li)) { err = "manifest.json: layer " + L.name + " derives from " + from->str + ", not another layer"; return false; }
            if (!parse_hex(over->str, L.over)) { err = "manifest.json: layer " + L.name + "'s derive.over is not #rrggbb"; return false; }
            if (!(mix->num >= 0 && mix->num <= 1)) { err = "manifest.json: layer " + L.name + "'s linear_mix is not in 0..1"; return false; }
            L.linear_mix = mix->num;
        }
        if (!read_pnm(dir + "/" + L.mask_file, "P5", scene.width, scene.height, data, off, err)) return false;
        const auto* m = reinterpret_cast<const uint8_t*>(data.data() + off);
        for (size_t k = 0; k < n; ++k) {
            if (m[k] == 255) L.pixels.push_back(uint32_t(k));
            else if (m[k] != 0) {
                err = L.mask_file + ": the byte " + std::to_string(m[k]) + " at x " + std::to_string(k % scene.width) +
                      ", y " + std::to_string(k / scene.width) + " (a mask is 0 or 255)";
                return false;
            }
        }
    }
    for (const Layer& L : scene.layers)   // a derive chain follows picked colours only
        if (L.derived && scene.layers[L.from].derived) {
            err = "manifest.json: layer " + L.name + " derives from the derived layer " + scene.layers[L.from].name; return false;
        }
    for (size_t k = 0; k < scene.layers.size(); ++k)
        if (scene.layers[k].name == act->str) scene.active = int(k);
    if (scene.active < 0) { err = "manifest.json: the active layer " + act->str + " is not among the layers"; return false; }
    if (scene.layers[scene.active].derived) { err = "manifest.json: the active layer " + act->str + " is derived"; return false; }
    scene_derive(scene);
    return true;
}

void scene_derive(Scene& scene) {
    for (Layer& L : scene.layers)
        if (L.derived) L.colour = lin_mix(scene.layers[L.from].colour, L.over, L.linear_mix);
}

void scene_paint_layers(const Scene& scene, uint32_t* picture) {
    for (const Layer& L : scene.layers) {
        const uint32_t w = word_of(L.colour);
        for (uint32_t k : L.pixels) picture[k] = w;
    }
}

void scene_paint(const Scene& scene, uint32_t* picture) {
    std::memcpy(picture, scene.background.data(), scene.background.size() * 4);
    scene_paint_layers(scene, picture);
}

namespace {

// a stored view lies in its ranges and gives its bytes (Pick's rule)
bool view_holds(const Pick& p) {
    return p.h >= 0 && p.h <= 360 && p.s >= 0 && p.s <= 1 && p.v >= 0 && p.v <= 1 && rgb_of_hsv(p.h, p.s, p.v) == p.rgb;
}

// one view number, the whole token: true and the double
bool read_view_number(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    char* end = nullptr;
    out = std::strtod(tok.c_str(), &end);
    return end == tok.c_str() + tok.size();
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
                std::string& err) {
    out.clear();
    entries.clear();
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
    const Json* hsv = st.get("hsv");   // absent in the previous build's file
    if (!ent || !ent->is_object() || (hsv && !hsv->is_object()) || st.obj.size() != (hsv ? 3u : 2u)) {
        err = "state.json: not {\"colours\": {...}, \"hsv\": {...}, \"entry\": {...}}"; return false;
    }
    for (const auto& kv : cols->obj) {
        Pick p;
        if (!kv.second.is_string() || !parse_hex(kv.second.str, p.rgb)) { err = "state.json: colours." + kv.first + " is not #rrggbb"; return false; }
        out[kv.first] = p;
    }
    if (hsv)
        for (const auto& kv : hsv->obj) {
            const auto c = out.find(kv.first);
            const Json& a = kv.second;
            if (c == out.end() || !a.is_array() || a.arr.size() != 3 || !a.arr[0].is_number() || !a.arr[1].is_number() ||
                !a.arr[2].is_number()) {
                err = "state.json: hsv." + kv.first + " is not [h, s, v] beside a colour"; return false;
            }
            Pick& p = c->second;
            p.has_hsv = true;
            p.h = a.arr[0].num;
            p.s = a.arr[1].num;
            p.v = a.arr[2].num;
            if (!view_holds(p)) { err = "state.json: hsv." + kv.first + " is not a view giving " + hex_of(p.rgb) + " (h 0..360, s and v 0..1)"; return false; }
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

bool picks_load(const std::string& path, const std::string& layer, std::vector<Pick>& out, std::string& err) {
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
        // the words, single spaces: <time> <layer...> #RRGGBB [hsv <h> <s> <v>]
        std::vector<std::string> w;
        for (size_t b = 0;;) {
            const size_t e = l.find(' ', b);
            w.push_back(l.substr(b, e == std::string::npos ? std::string::npos : e - b));
            if (e == std::string::npos) break;
            b = e + 1;
        }
        const bool view = w.size() >= 7 && w[w.size() - 4] == "hsv";
        const size_t hex_at = w.size() - (view ? 5 : 1);
        Pick p;
        std::string name;
        for (size_t k = 1; k < hex_at && w.size() >= 3; ++k) name += (k > 1 ? " " : "") + w[k];
        if (w.size() < 3 || w[0].empty() || name.empty() || !parse_hex(w[hex_at], p.rgb) ||
            (view && !(read_view_number(w[w.size() - 3], p.h) && read_view_number(w[w.size() - 2], p.s) &&
                       read_view_number(w[w.size() - 1], p.v)))) {
            err = where + " is not \"<time> <layer> #RRGGBB hsv <h> <s> <v>\" (nor \"<time> <layer> #RRGGBB\")"; return false;
        }
        p.has_hsv = view;
        if (view && !view_holds(p)) { err = where + "'s hsv is not a view giving " + hex_of(p.rgb) + " (h 0..360, s and v 0..1)"; return false; }
        if (name == layer) out.push_back(p);
    }
    return true;
}
