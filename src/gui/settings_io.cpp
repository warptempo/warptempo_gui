#include "settings_io.h"

#include "app_state.h"
#include "time_format.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

namespace {

std::string trim_ws(const std::string& s) {
    size_t a = 0;
    while (a < s.size() &&
           std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a &&
           std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool parse_int64_full(const std::string& s, int64_t& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    out = static_cast<int64_t>(v);
    return true;
}

bool parse_int_full(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() ||
        v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

bool parse_float_full(const std::string& s, float& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

} // namespace

// MM:SS.mmm shape validator: exactly 9 chars, ':' at index 2, '.' at
// index 5, digits elsewhere. Matches the canonical marker timestamp
// shape parse_timestamp expects. Returns true if parse_timestamp can
// be safely called on `s`. Exposed so settings_editor.cpp can route
// trim_begin / trim_end values through the same predicate.
bool is_settings_timestamp(const std::string& s) {
    if (s.size() != 9) return false;
    if (s[2] != ':' || s[5] != '.') return false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == 2 || i == 5) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

bool create_if_missing(const std::filesystem::path& p,
                       const std::string& contents) {
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) return true;
    std::ofstream f(p);
    if (!f) {
        std::fprintf(stderr,
                     "warptempo_gui: could not create '%s'\n",
                     p.string().c_str());
        return false;
    }
    f << contents;
    return static_cast<bool>(f);
}

bool parse_settings_file(const std::string& path, ParsedSettings& out) {
    out = ParsedSettings{};
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return true;  // nothing to load
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#') continue;
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) continue;
        const std::string key   = trim_ws(trimmed.substr(0, eq));
        const std::string value = trim_ws(trimmed.substr(eq + 1));
        if (key.empty()) continue;

        if (key == "tab_a_viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_a_vp = true; out.tab_a_vp = v; }
        } else if (key == "tab_a_zoom") {
            int v;
            if (parse_int_full(value, v)) { out.has_tab_a_zoom = true; out.tab_a_zoom = v; }
        } else if (key == "tab_a_playhead") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_a_ph = true; out.tab_a_ph = v; }
        } else if (key == "tab_b_viewport_start") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_b_vp = true; out.tab_b_vp = v; }
        } else if (key == "tab_b_zoom") {
            int v;
            if (parse_int_full(value, v)) { out.has_tab_b_zoom = true; out.tab_b_zoom = v; }
        } else if (key == "tab_b_playhead") {
            int64_t v;
            if (parse_int64_full(value, v)) { out.has_tab_b_ph = true; out.tab_b_ph = v; }
        } else if (key == "follow") {
            std::string lower = value;
            for (char& c : lower) c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (lower == "true")       { out.has_follow = true; out.follow = true;  }
            else if (lower == "false") { out.has_follow = true; out.follow = false; }
            // Any other value: silent-skip; default (true) applies at the call site.
        } else if (key == "active_mode") {
            // Case-sensitive "W" / "P" — these literals cross the engine
            // boundary. Anything else silent-skips like the `follow` parser.
            if (value == "W") { out.has_active_mode = true; out.active_mode = 'W'; }
            else if (value == "P") { out.has_active_mode = true; out.active_mode = 'P'; }
        } else if (key == "playback_speed") {
            float v;
            if (parse_float_full(value, v) && v > 0.0f) {
                out.has_playback_speed = true;
                out.playback_speed = v;
            }
        } else if (key == "trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_trim_begin = true;
                out.trim_begin     = parse_timestamp(value);
            }
        } else if (key == "trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_trim_end = true;
                out.trim_end     = parse_timestamp(value);
            }
        } else if (key == "tab_a_trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_tab_a_trim_begin = true;
                out.tab_a_trim_begin     = parse_timestamp(value);
            }
        } else if (key == "tab_a_trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_tab_a_trim_end = true;
                out.tab_a_trim_end     = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_begin") {
            if (is_settings_timestamp(value)) {
                out.has_tab_b_trim_begin = true;
                out.tab_b_trim_begin     = parse_timestamp(value);
            }
        } else if (key == "tab_b_trim_end") {
            if (is_settings_timestamp(value)) {
                out.has_tab_b_trim_end = true;
                out.tab_b_trim_end     = parse_timestamp(value);
            }
        } else {
            out.passthrough.emplace_back(key, value);
        }
    }
    return true;
}

std::string format_default_settings_template(const std::string& stem,
                                             const std::string& ext_no_dot) {
    std::string s;
    s += "title=";       s += stem; s += "-rendered\n";
    s += "audio_input="; s += stem; s += '.'; s += ext_no_dot; s += '\n';
    s += "scale=1.000000\n";
    s += "engine=warptempo\n";
    s += "N=4096\n";
    s += "fftw_threads=16\n";
    s += "limiter_enabled=false\n";
    s += "phase_reset_offset_R_s=1.000000\n";
    s += "active_mode=W\n";
    s += "playback_speed=1.000000\n";
    s += "follow=true\n";
    s += "tab_a_viewport_start=0\n";
    s += "tab_a_zoom=0\n";
    s += "tab_a_playhead=0\n";
    s += "tab_b_viewport_start=0\n";
    s += "tab_b_zoom=0\n";
    s += "tab_b_playhead=0\n";
    return s;
}

bool write_settings_file(
    const std::string& path,
    const ViewState& tab_a,
    const ViewState& tab_b,
    bool follow,
    char active_mode,
    float playback_speed,
    const std::vector<std::pair<std::string, std::string>>& passthrough) {
    std::string data;
    for (const auto& kv : passthrough) {
        data += kv.first;
        data += '=';
        data += kv.second;
        data += '\n';
    }
    data += "follow=";
    data += follow ? "true" : "false";
    data += '\n';
    data += "active_mode=";
    data += active_mode;
    data += '\n';
    char fbuf[32];
    std::snprintf(fbuf, sizeof(fbuf), "%.6f", playback_speed);
    data += "playback_speed=";
    data += fbuf;
    data += '\n';
    if (tab_a.has_trim_begin) {
        data += "tab_a_trim_begin=";
        data += format_timestamp(tab_a.trim_begin_seconds);
        data += '\n';
    }
    if (tab_a.has_trim_end) {
        data += "tab_a_trim_end=";
        data += format_timestamp(tab_a.trim_end_seconds);
        data += '\n';
    }
    if (tab_b.has_trim_begin) {
        data += "tab_b_trim_begin=";
        data += format_timestamp(tab_b.trim_begin_seconds);
        data += '\n';
    }
    if (tab_b.has_trim_end) {
        data += "tab_b_trim_end=";
        data += format_timestamp(tab_b.trim_end_seconds);
        data += '\n';
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(tab_a.viewport_start_sample));
    data += "tab_a_viewport_start="; data += buf; data += '\n';
    std::snprintf(buf, sizeof(buf), "%d", tab_a.zoom_level);
    data += "tab_a_zoom=";            data += buf; data += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(tab_a.playhead_sample));
    data += "tab_a_playhead=";        data += buf; data += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(tab_b.viewport_start_sample));
    data += "tab_b_viewport_start="; data += buf; data += '\n';
    std::snprintf(buf, sizeof(buf), "%d", tab_b.zoom_level);
    data += "tab_b_zoom=";            data += buf; data += '\n';
    std::snprintf(buf, sizeof(buf), "%lld",
                  static_cast<long long>(tab_b.playhead_sample));
    data += "tab_b_playhead=";        data += buf; data += '\n';

    mode_t mode = 0644;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) mode = st.st_mode & 07777;

    const std::string tmp_path = path + ".tmp";
    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return false;

    size_t written = 0;
    while (written < data.size()) {
        const ssize_t n = ::write(fd, data.data() + written,
                                  data.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            ::close(fd);
            ::unlink(tmp_path.c_str());
            return false;
        }
        written += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp_path.c_str());
        return false;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    ::chmod(tmp_path.c_str(), mode);
    if (::rename(tmp_path.c_str(), path.c_str()) != 0) {
        ::unlink(tmp_path.c_str());
        return false;
    }
    return true;
}
