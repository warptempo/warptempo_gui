#include "json.h"

#include <cstdlib>

namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) ++i;
    }
    bool fail(const std::string& what) {
        if (err.empty()) err = what + " at byte " + std::to_string(i);
        return false;
    }
    bool lit(const char* w) {
        size_t n = 0;
        while (w[n]) ++n;
        if (s.compare(i, n, w) != 0) return fail(std::string("expected ") + w);
        i += n;
        return true;
    }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return fail("expected a string");
        ++i;
        for (;;) {
            if (i >= s.size()) return fail("unterminated string");
            const char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (i >= s.size()) return fail("unterminated escape");
            const char e = s[i++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i + 4 > s.size()) return fail("short \\u escape");
                    const long v = std::strtol(s.substr(i, 4).c_str(), nullptr, 16);
                    if (v > 0x7F) return fail("a non-ASCII \\u escape");
                    out.push_back(char(v));
                    i += 4;
                    break;
                }
                default: return fail("bad escape");
            }
        }
    }
    bool value(Json& v) {
        ws();
        if (i >= s.size()) return fail("unexpected end");
        const char c = s[i];
        if (c == '{') {
            v.kind = Json::Kind::Object;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i >= s.size() || s[i] != ':') return fail("expected ':'");
                ++i;
                Json child;
                if (!value(child)) return false;
                v.obj.emplace_back(std::move(k), std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.kind = Json::Kind::Array;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                Json child;
                if (!value(child)) return false;
                v.arr.push_back(std::move(child));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') { v.kind = Json::Kind::String; return string(v.str); }
        if (c == 't') { v.kind = Json::Kind::Bool; v.b = true; return lit("true"); }
        if (c == 'f') { v.kind = Json::Kind::Bool; v.b = false; return lit("false"); }
        if (c == 'n') { v.kind = Json::Kind::Null; return lit("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char* begin = s.c_str() + i;
            char* end = nullptr;
            v.kind = Json::Kind::Number;
            v.num = std::strtod(begin, &end);
            if (end == begin) return fail("bad number");
            i += size_t(end - begin);
            return true;
        }
        return fail(std::string("unexpected '") + c + "'");
    }
};

} // namespace

bool json_parse(const std::string& text, Json& out, std::string& err) {
    Parser p{text, 0, {}};
    out = Json{};
    if (!p.value(out)) { err = p.err; return false; }
    p.ws();
    if (p.i != text.size()) { p.fail("trailing text"); err = p.err; return false; }
    return true;
}
