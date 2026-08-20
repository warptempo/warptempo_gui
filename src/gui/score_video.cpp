// The score-video act's whole implementation: the map read, the measure
// resolution walk, the time lookup, and the one line written to mpv. The act's
// contract, its refusal taxonomy and the folder law are at score_video.h.

#include "score_video.h"

#include "app_state.h"
#include "marker_measure.h"
#include "phaseresetmarkers.h"
#include "warpmarkers.h"

#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// The child's environment for the mpv spawn below — the audio player's own
// arrangement (spawn_audio_player, input_key_dispatch.cpp), POSIX's one handle
// on the process environment.
extern char** environ;

namespace {

// ---------------------------------------------------------------------------
// THE MAP'S NUMBER READERS.
//
// They are deliberately NOT the product's canonical spellings (frame_format.h,
// value_format.h): those exist so that a value the GUI writes reads back byte
// for byte, and the GUI writes no byte of this file. What is asked here is only
// "is this a number at all", full-string, finite.

bool parse_full_double(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string s(text);
    errno = 0;
    char*        end = nullptr;
    const double v   = std::strtod(s.c_str(), &end);
    if (errno == ERANGE) return false;
    if (end != s.c_str() + s.size()) return false;
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parse_full_int(std::string_view text, int64_t& out) {
    if (text.empty()) return false;
    const std::string s(text);
    errno = 0;
    char*         end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (errno == ERANGE) return false;
    if (end != s.c_str() + s.size()) return false;
    out = static_cast<int64_t>(v);
    return true;
}

// ---------------------------------------------------------------------------
// THE RESOLVED SCORE POSITION — a measure number plus a proper fraction of the
// way through it, in EXACT rational arithmetic. It is the measure grammar's own
// value domain (marker_measure.h), summed down a '+' chain; nothing rounds
// until the time lookup, which is approximate by design and says so.
struct ScorePosition {
    int64_t whole = 0;
    int64_t num   = 0;  // 0 <= num < den
    int64_t den   = 1;
};

// Add one parsed measure token's magnitude to a running position. False on
// ARITHMETIC OVERFLOW and only that — a chain long enough to blow an int64
// denominator (co-prime denominators multiplying without bound) refuses the act
// rather than jumping to a confident wrong bar. It cannot be reached by any
// score a person would author; the guard exists because silence is the wrong
// failure for a number that stopped being the number.
bool add_measure_value(ScorePosition& p, const MarkerMeasureValue& v) {
    if (v.whole > 0) {
        if (p.whole > INT64_MAX - v.whole) return false;
        p.whole += v.whole;
    }
    if (v.num > 0) {
        const int64_t g = std::gcd(p.den, v.den);
        const int64_t l = p.den / g;
        if (l > INT64_MAX / v.den) return false;
        const int64_t  nden = l * v.den;
        __int128       sum  = static_cast<__int128>(p.num) * (nden / p.den) +
                             static_cast<__int128>(v.num) * (nden / v.den);
        if (sum >= nden) {
            // The fraction carried: one whole measure out of it, so the
            // fraction stays proper the way the grammar's own values are.
            sum -= nden;
            if (p.whole == INT64_MAX) return false;
            p.whole += 1;
        }
        const int64_t n = static_cast<int64_t>(sum);
        if (n == 0) {
            p.num = 0;
            p.den = 1;
        } else {
            const int64_t r = std::gcd(n, nden);
            p.num = n / r;
            p.den = nden / r;
        }
    }
    return true;
}

// THE CONSUMER-SIDE RESOLUTION WALK, and marker_measure.h is the authority for
// every rule it obeys: a DIRECT measure is its own value; a '+' OFFSET adds to
// the IMMEDIATE PREDECESSOR in the SAME column with no fallback scan past it;
// chains resolve; a BROKEN LINK — a predecessor carrying no measure, a
// predecessor off the head of the store, or one that is itself unresolvable —
// leaves this marker UNRESOLVED. Unresolved is VALID (the grammar is validity,
// resolution is not); it gates this consumer alone, which answers it with the
// act's silent no-op.
//
// The parse is marker_measure.h's own, never a second reading of the token.
//
// ONE WALK SERVES BOTH COLUMNS because the field is on the shared serialized
// base (WarpMarker::measure / PhaseResetMarker::measure) — the measure is the
// one marker field with no column asymmetry at all, which is what makes the
// home-view binding's fourth exception a rule rather than a pair of them.
template <typename MarkerT>
bool resolve_measure_position(const std::vector<MarkerT>& mv, int idx,
                              ScorePosition& out) {
    if (idx < 0 || static_cast<size_t>(idx) >= mv.size()) return false;
    // The chain, successor first: walk back over '+' tokens until a direct
    // measure bottoms it out, then sum forward from that direct anchor.
    std::vector<MarkerMeasureValue> chain;
    for (int j = idx; ; --j) {
        if (j < 0) return false;
        const std::string& text = mv[static_cast<size_t>(j)].measure;
        if (text.empty()) return false;
        MarkerMeasureValue v;
        std::string        err;
        if (!parse_marker_measure(text, v, err)) return false;
        chain.push_back(v);
        if (!v.is_offset) break;
    }
    ScorePosition p;
    for (size_t k = chain.size(); k-- > 0;) {
        if (!add_measure_value(p, chain[k])) return false;
    }
    out = p;
    return true;
}

// ---------------------------------------------------------------------------
// THE TIME LOOKUP — linear interpolation between the two bracketing anchors,
// clamped to the first and last. It is APPROXIMATE BY DESIGN and
// architect-accepted as such: the map anchors whole measures at page starts, so
// a fraction of a measure is interpolated across the anchor interval rather
// than measured. Nothing is stored; the answer is a seek.
bool score_video_time_for(const GuiScoreVideoMap& map, const ScorePosition& pos,
                          double& out) {
    if (map.anchors.empty()) return false;
    const double p = static_cast<double>(pos.whole) +
                     (pos.num > 0 ? static_cast<double>(pos.num) /
                                        static_cast<double>(pos.den)
                                  : 0.0);
    if (p <= static_cast<double>(map.anchors.front().measure)) {
        out = map.anchors.front().seconds;
        return true;
    }
    for (size_t i = 0; i + 1 < map.anchors.size(); ++i) {
        const GuiScoreVideoAnchor& a = map.anchors[i];
        const GuiScoreVideoAnchor& b = map.anchors[i + 1];
        if (p >= static_cast<double>(b.measure)) continue;
        // Strictly increasing measures are the parser's own guarantee, so the
        // span below is never zero.
        const double span = static_cast<double>(b.measure - a.measure);
        out = a.seconds +
              (p - static_cast<double>(a.measure)) / span * (b.seconds - a.seconds);
        return true;
    }
    out = map.anchors.back().seconds;
    return true;
}

// ---------------------------------------------------------------------------
// MPV.

// The single instance's socket, one fixed path per user so that every jump in
// every session reaches the same window. $XDG_RUNTIME_DIR is where a per-user
// socket belongs; the /tmp fallback carries the uid for the same reason the
// runtime dir does.
std::string mpv_socket_path() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime && *runtime) {
        return std::string(runtime) + "/warptempo-mpv.sock";
    }
    return "/tmp/warptempo-mpv-" +
           std::to_string(static_cast<long long>(::getuid())) + ".sock";
}

// One JSON string body. Bytes >= 0x80 pass through verbatim: a path is FREE
// TEXT (the UTF-8 class, conventions.md) and JSON strings are UTF-8, so the
// escaping owes only the two structural characters and the control range.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const char raw : s) {
        const unsigned char c = static_cast<unsigned char>(raw);
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x",
                              static_cast<unsigned>(c));
                out += buf;
            } else {
                out += raw;
            }
            break;
        }
    }
    return out;
}

// Send ONE command line to a standing mpv. Returns false when nothing was
// listening (the ordinary "no instance yet" answer, which the caller turns into
// a spawn) and sets `wrote_failed` when a CONNECTED socket then refused the
// write — a different fact, and the one the caller reports.
//
// No reply is read: mpv answers every command with a JSON line, and this act
// has nothing to do with the answer. The write timeout keeps a wedged mpv from
// stalling the GUI thread; SIGPIPE is SIG_IGN process-wide (main.cpp), so a
// dead peer returns EPIPE here rather than killing the process.
bool send_mpv_command(const std::string& socket_path, const std::string& line,
                      bool& write_failed) {
    write_failed = false;
    sockaddr_un addr{};
    if (socket_path.size() + 1 > sizeof(addr.sun_path)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size());
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 200000;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    size_t off = 0;
    while (off < line.size()) {
        const ssize_t n = ::write(fd, line.data() + off, line.size() - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        write_failed = true;
        break;
    }
    ::close(fd);
    return !write_failed;
}

// Start mpv DETACHED on the video, already seeked, with its IPC server on the
// socket the next jump will find. posix_spawnp with SETSIGDEF is the product's
// one detached-spawn idiom (spawn_audio_player, input_key_dispatch.cpp, which
// carries the reasoning for the reset dispositions); SIGCHLD is SIG_IGN from
// startup, so the child auto-reaps and nothing here waits.
//
// TWO THINGS THIS SPAWN ADDS to that idiom, both for a child that owns a
// window and a terminal-shaped stdio: its standard streams go to /dev/null (a
// player's status lines would otherwise interleave with the GUI's own stderr,
// and its stdin would compete for the launching terminal), and it takes a
// SESSION of its own where the platform offers one.
//
// argv exec, NEVER a shell (the project's standing rule): a project folder's
// name carries spaces and reaches mpv as one argv element with no quoting rules
// in between. `--` guards a dash-leading path.
bool spawn_mpv(const std::string& socket_path, const std::string& video,
               const std::string& start_seconds) {
    const std::string ipc_arg   = "--input-ipc-server=" + socket_path;
    const std::string start_arg = "--start=" + start_seconds;
    char* argv[] = {const_cast<char*>("mpv"),
                    const_cast<char*>(ipc_arg.c_str()),
                    const_cast<char*>(start_arg.c_str()),
                    const_cast<char*>("--"),
                    const_cast<char*>(video.c_str()),
                    nullptr};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null",
                                     O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null",
                                     O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null",
                                     O_WRONLY, 0);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGCHLD);
    sigaddset(&def, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &def);
    short flags = POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_SETSID
    flags |= POSIX_SPAWN_SETSID;
#endif
    posix_spawnattr_setflags(&attr, flags);

    pid_t     pid = 0;
    const int rc  = posix_spawnp(&pid, "mpv", &actions, &attr, argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    return rc == 0;
}

}  // namespace

// THE MAP PARSER, AND ITS FAILURE CLASS, named here because
// validation_topology.md asks every guard to classify: THIS FILE IS NOT IN THE
// ADVERSARIAL CLASS AND THE TWO-CATEGORY RULE DOES NOT REACH IT (architect
// 2026-08-20). That rule is about the SIDECARS — a state the GUI can commit
// always loads, a state it can never produce hard-fails the load — and it rests
// on the GUI being the writer. sheet.map has no GUI writer at all: it is a
// local, gitignored artifact of tools/extract_sheet_map.py, it is never saved,
// never committed, never rendered from, and it carries no authored musical
// content. So a malformed map is not an attack on the product's data model; it
// is an ENVIRONMENT PRECONDITION that failed (the out-of-topology label in
// validation_topology.md — guards the launch, not the data), and the whole act
// refuses SILENTLY, exactly as a missing map does. There is deliberately no
// diagnostic and no error vocabulary: the two states a user can be in are "the
// jump worked" and "there is no usable map here", and a line number in a file
// he did not write serves neither.
//
// The tolerated shapes are the tool's own output plus what a hand edit adds: an
// unknown `#` line is ignored, a blank line is ignored, and a trailing CR is
// stripped. Everything else is judged.
GuiScoreVideoMap load_score_video_map(const std::string& project_dir) {
    GuiScoreVideoMap map;
    const std::filesystem::path path =
        std::filesystem::path(project_dir) / "sheet" / "sheet.map";
    std::ifstream in(path);
    if (!in) return map;

    bool    have_previous = false;
    double  prev_seconds  = 0.0;
    int64_t prev_measure  = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.front() == '#') {
            // `# src <name>` takes the REST of the line verbatim: a rip's file
            // name carries spaces, so it is not a token. It is a BASENAME by
            // definition (the tool writes os.path.basename) and it is held to
            // that here — a value carrying a separator or naming a directory
            // entry would let the header point the act outside sheet/src/,
            // which the format never means.
            if (line.rfind("# src ", 0) == 0) {
                map.src = line.substr(6);
                if (map.src.empty() || map.src == "." || map.src == ".." ||
                    map.src.find('/') != std::string::npos)
                    return {};
                continue;
            }
            if (line.rfind("# window ", 0) == 0) {
                const std::string rest = line.substr(9);
                const size_t      sp   = rest.find(' ');
                if (sp == std::string::npos) return {};
                double start = 0.0;
                if (!parse_full_double(std::string_view(rest).substr(0, sp),
                                       start))
                    return {};
                if (start < 0.0) return {};
                const std::string_view end_field =
                    std::string_view(rest).substr(sp + 1);
                double end_seconds = 0.0;
                if (end_field != "eof" &&
                    !parse_full_double(end_field, end_seconds))
                    return {};
                map.window_start = start;
                continue;
            }
            continue;  // an unknown header line says nothing to this reader
        }
        const size_t bar = line.find('|');
        if (bar == std::string::npos) return {};
        double  seconds = 0.0;
        int64_t measure = 0;
        if (!parse_full_double(std::string_view(line).substr(0, bar), seconds))
            return {};
        if (!parse_full_int(std::string_view(line).substr(bar + 1), measure))
            return {};
        if (seconds < 0.0 || measure < 1) return {};
        // STRICTLY INCREASING IN BOTH FIELDS, which is what makes the lookup's
        // bracketing walk and its non-zero span safe rather than checked again.
        if (have_previous && (seconds <= prev_seconds || measure <= prev_measure))
            return {};
        map.anchors.push_back({seconds, measure});
        prev_seconds  = seconds;
        prev_measure  = measure;
        have_previous = true;
    }
    if (map.anchors.empty()) return {};
    map.ok = true;
    return map;
}

// THE ACT, top to bottom. Every arm that returns early is a consumed silent
// no-op — the taxonomy is at the header, and the ONE stderr line this act can
// print is the mpv write failure at the bottom.
void run_score_video_jump(const AppState& app) {
    if (app.source_audio_path.empty()) return;
    // THE SUBJECT IS THE FOCUS, exactly the bare-`/` arm's own: the caller has
    // already repaired it, and nothing focused is a no-op.
    const int idx = app.last_selected_marker;
    if (idx < 0) return;

    ScorePosition pos;
    const bool resolved =
        (app.active_markers_view == 'P')
            ? resolve_measure_position(app.phaseresetmarkers.markers(), idx, pos)
            : resolve_measure_position(app.warpmarkers.markers(), idx, pos);
    if (!resolved) return;

    // THE PROJECT DIRECTORY IS THE SOURCE'S OWN PARENT FOLDER — the GitHub
    // recheck's folder law, read here off the path alone with no git in it (the
    // map is not in the repository at all).
    const std::filesystem::path project =
        std::filesystem::path(app.source_audio_path).parent_path();
    const GuiScoreVideoMap map = load_score_video_map(project.string());
    if (!map.ok) return;

    double t = 0.0;
    if (!score_video_time_for(map, pos, t)) return;

    // THE TRIMMED CLIP FIRST, THE FULL RIP BEHIND IT. sheet.webm is the map's
    // own window already cut, so the map's times are its times. Failing that
    // the header's `# src` names the untrimmed file the map was extracted from,
    // where the same moment sits window_start later — which is what lets one
    // stable rip serve four movements' maps with no trimmed copy of anything.
    std::error_code       ec;
    std::filesystem::path video = project / "sheet" / "sheet.webm";
    double                seek  = t;
    if (!std::filesystem::exists(video, ec) || ec) {
        if (map.src.empty()) return;
        ec.clear();
        video = project / "sheet" / "src" / map.src;
        if (!std::filesystem::exists(video, ec) || ec) return;
        seek = map.window_start + t;
    }

    char seek_text[64];
    std::snprintf(seek_text, sizeof seek_text, "%.3f", seek);
    const std::string video_path = video.string();
    const std::string socket_path = mpv_socket_path();

    // ONE COMMAND, and `loadfile` is the whole of it: it loads the file,
    // REPLACES whatever is showing and applies the start option in the same
    // act, so a jump within the same video and a jump into a fresh one are one
    // code path. A same-file jump reloads rather than seeks — accepted, mpv
    // being fast at it and the alternative being a second command shape and a
    // reply to parse.
    const std::string command = "{\"command\":[\"loadfile\",\"" +
                                json_escape(video_path) +
                                "\",\"replace\",-1,\"start=" + seek_text +
                                "\"]}\n";
    bool write_failed = false;
    if (send_mpv_command(socket_path, command, write_failed)) return;
    if (write_failed) {
        std::fprintf(stderr,
                     "warptempo_gui: score video: mpv did not take the seek\n");
        return;
    }
    // NOTHING WAS LISTENING, so this jump is the one that starts the window.
    if (!spawn_mpv(socket_path, video_path, seek_text)) {
        std::fprintf(stderr,
                     "warptempo_gui: score video: could not start mpv\n");
    }
}
