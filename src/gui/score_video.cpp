// The score-video act's whole implementation: the map read, the measure
// resolution walk, the time lookup, and the command batch written to mpv (the
// jump, the audition's speed, and pitch correction off — three newline-
// delimited commands on one connection). The act's contract, its refusal
// taxonomy and the folder law are at score_video.h.

#include "score_video.h"

#include "app_state.h"
#include "marker_measure.h"
#include "phaseresetmarkers.h"
#include "warpmarkers.h"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <signal.h>
#include <sys/socket.h>
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

// THE ACCEPTED TIME DOMAIN, one bound for every second this file handles: the
// header's window start, every anchor, and the seek that comes out of them.
// 10^7 seconds is about 115 days — no recording approaches it, so the bound
// refuses nothing real — and it is what makes the arithmetic downstream honest
// rather than merely finite. TWO THINGS REST ON IT: window_start + t cannot
// leave the domain by adding two in-domain values, and `%.3f` of an in-domain
// value spells at most twelve characters, so the fixed buffer it is formatted
// into can never truncate a number into a DIFFERENT number. A finite double
// that is merely enormous would pass a plain isfinite check and do both.
constexpr double kScoreVideoMaxSeconds = 1e7;

bool score_video_time_in_domain(double seconds) {
    return std::isfinite(seconds) && seconds >= 0.0 &&
           seconds <= kScoreVideoMaxSeconds;
}

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

// One CANONICAL unsigned decimal field of a map line — digits only, no sign, no
// leading zero on a multi-digit value, at most `max_digits` of them. The
// measure and its section are held to the MEASURE GRAMMAR's own spelling rules
// (marker_measure.h) rather than to strtoll's, so `+12`, `012` and `-2` are
// refused here exactly as they are in a marker's own field: one spelling per
// value, on both sides of the same idea.
bool parse_canonical_field(std::string_view text, size_t max_digits,
                           int64_t& out) {
    if (text.empty() || text.size() > max_digits) return false;
    if (text.size() > 1 && text.front() == '0') return false;
    int64_t v = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}

// ---------------------------------------------------------------------------
// THE RESOLVED SCORE POSITION — a (SECTION, RATIONAL) pair since 2026-08-20:
// the printed measure number plus a proper fraction of the way through it, and
// the printed NUMBERING it belongs to. It is the measure grammar's own value
// domain (marker_measure.h, which owns both halves), summed down a '+' chain;
// nothing rounds until the time lookup, which is approximate by design and says
// so. `section` rests at 1, the value a bare spelling carries.
struct ScorePosition {
    int64_t section = 1;
    int64_t whole   = 0;
    int64_t num     = 0;  // 0 <= num < den
    int64_t den     = 1;
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
    // THE CHAIN'S SECTION IS ITS DIRECT ANCHOR'S, and taking it here — from
    // chain.back(), the one direct token the walk bottomed out on — is the
    // never-crosses-sections ruling in code: no offset above it carries a
    // section of its own (the grammar refuses `+2:1`), and none of the
    // arithmetic below can move a number out of one printed numbering.
    ScorePosition p;
    p.section = chain.back().section;
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
//
// IT WORKS WITHIN ONE SECTION AND NEVER ACROSS TWO (2026-08-20, with the
// printed-number ruling). The anchors of OTHER sections are not merely skipped
// as unmatched — they are excluded from the bracketing entirely, because
// measure numbers are only comparable inside the numbering that printed them:
// with a trio restarting at 1, bar 12 of section 2 sits AFTER bar 40 of section
// 1 in time while being a smaller number, and a walk that mixed them would
// interpolate between two unrelated bars and land confidently in the wrong
// music. So the walk takes this section's anchors in map order and clamps at
// THIS SECTION'S first and last — which is also what makes a position past a
// section's end land on that section's last page rather than sliding into the
// next section's opening.
//
// A SECTION THE MAP DOES NOT CARRY IS A REFUSAL, the act's usual silent no-op:
// a measure naming section 3 of a two-section map names a place this video has
// no page for, and guessing one would be the confident wrong answer this whole
// file refuses to give.
bool score_video_time_for(const GuiScoreVideoMap& map, const ScorePosition& pos,
                          double& out) {
    const double p = static_cast<double>(pos.whole) +
                     (pos.num > 0 ? static_cast<double>(pos.num) /
                                        static_cast<double>(pos.den)
                                  : 0.0);
    const GuiScoreVideoAnchor* prev = nullptr;
    const GuiScoreVideoAnchor* first = nullptr;
    for (const GuiScoreVideoAnchor& a : map.anchors) {
        if (a.section != pos.section) continue;
        if (first == nullptr) {
            first = &a;
            // BEFORE THIS SECTION'S FIRST ANCHOR: clamp onto it.
            if (p <= static_cast<double>(a.measure)) {
                out = a.seconds;
                return true;
            }
        }
        if (prev != nullptr && p < static_cast<double>(a.measure)) {
            // Strictly increasing measures WITHIN a section are the parser's
            // own guarantee, so the span below is never zero.
            const double span =
                static_cast<double>(a.measure - prev->measure);
            out = prev->seconds +
                  (p - static_cast<double>(prev->measure)) / span *
                      (a.seconds - prev->seconds);
            return true;
        }
        prev = &a;
    }
    if (prev == nullptr) return false;  // the map carries no such section
    out = prev->seconds;                // past this section's last anchor
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

// THE WHOLE IPC BUDGET, connect and write together. This runs ON THE GUI
// THREAD, so the bound is not a nicety: it is what keeps Shift+`/` from
// freezing the window on a peer that owns the socket and is not servicing it.
constexpr int kScoreVideoIpcMs = 200;

// Wait for `fd` to become writable, or for the deadline. Returns 1 ready,
// 0 timed out, -1 error. EINTR re-polls against the same deadline rather than
// restarting the budget, so the total stays bounded however many signals land.
int wait_writable(int fd, int64_t deadline_ms) {
    for (;;) {
        const int64_t remaining = deadline_ms - monotonic_ms();
        if (remaining <= 0) return 0;
        pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLOUT;
        const int rc = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (rc > 0) return 1;
        if (rc == 0) return 0;
        if (errno == EINTR) continue;
        return -1;
    }
}

// Send one COMMAND BATCH to a standing mpv — `payload` is one or more
// newline-terminated JSON commands, written whole on one connection (mpv's IPC
// is line-delimited, so the batch costs one connect, one deadline and one write
// loop rather than three). Returns false when nothing was listening (the
// ordinary "no instance yet" answer, which the caller turns into a spawn) and
// sets `write_failed` when the socket WAS reached and the payload still could
// not be delivered — a different fact, and the one the caller reports.
//
// NOTHING HERE BLOCKS WITHOUT A BOUND. The socket is NONBLOCKING FROM BIRTH,
// which is the point: a blocking AF_UNIX connect can wait on a listener whose
// accept queue is full — a stopped or wedged mpv still owning the socket — and
// no send timeout applies to it, because the connect has not returned yet to be
// timed. So connect and write both run under one `monotonic_ms` deadline
// (kScoreVideoIpcMs), and the worst case for the GUI thread is that budget.
//
// THE TIMEOUT IS NOT A "NOT LISTENING" ANSWER, and the distinction decides
// whether a second mpv gets launched: an immediate connect error (no socket
// file, nothing bound, a stale file refusing) means there is no instance, so
// the caller spawns one; a connect that could not COMPLETE and then times out
// means SOMETHING IS THERE and is not answering, so this reports instead —
// spawning a rival that could not bind the occupied path would be a silent
// nothing.
//
// AF_UNIX SAYS EAGAIN WHERE TCP SAYS EINPROGRESS, and both mean the same thing
// here: the listener exists but its ACCEPT QUEUE IS FULL — a stopped or wedged
// mpv still owning the socket — so the connection is pending rather than
// refused. Reading only EINPROGRESS sent exactly the case this contract was
// written for down the "nothing is listening" path and launched a rival mpv
// against an occupied socket. Both codes now enter the poll + SO_ERROR wait
// below, under the one deadline.
//
// No reply is read: mpv answers every command with a JSON line, and this act
// has nothing to do with the answer — which is exactly why the caller's
// diagnostic says the WRITE failed and never that mpv refused anything.
// SIGPIPE is SIG_IGN process-wide (main.cpp), so a peer that goes away mid-line
// returns EPIPE here rather than killing the process.
bool send_mpv_command(const std::string& socket_path,
                      const std::string& payload, bool& write_failed) {
    write_failed = false;
    sockaddr_un addr{};
    if (socket_path.size() + 1 > sizeof(addr.sun_path)) return false;
    const int fd =
        ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socket_path.c_str(), socket_path.size());

    const int64_t deadline = monotonic_ms() + kScoreVideoIpcMs;
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) != 0) {
        if (errno != EINPROGRESS && errno != EAGAIN && errno != EWOULDBLOCK) {
            ::close(fd);  // nothing is listening: the caller's spawn answer
            return false;
        }
        const int ready = wait_writable(fd, deadline);
        if (ready <= 0) {
            // Timed out or poll failed on a connect that was PENDING rather
            // than refused: reached, unusable, reported rather than respawned.
            ::close(fd);
            write_failed = true;
            return false;
        }
        int       err = 0;
        socklen_t len = sizeof(err);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 ||
            err != 0) {
            ::close(fd);  // the connect completed as a refusal: not listening
            return false;
        }
    }

    size_t off = 0;
    while (off < payload.size()) {
        const ssize_t n =
            ::write(fd, payload.data() + off, payload.size() - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait_writable(fd, deadline) == 1) continue;
        }
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
// THE FIRST JUMP CARRIES THE SPEED TOO, as OPTIONS rather than as commands:
// the instance this starts must open at the same rate a running one would be
// sent to, or the very first jump of a session would play at 1.0 and every
// later one at the audition's rate. `--speed` and `--audio-pitch-correction=no`
// are the command-line twins of two of the three properties the IPC path sets
// (`--fullscreen` below is the third's), and the varispeed reasoning for
// turning correction off is at that site.
//
// IT OPENS FULLSCREEN, and that is product intent (architect 2026-08-20): the
// score is a READING SURFACE on both hosts — a page of music in a windowed
// player on the rig's 1024x600 glass is unreadable, and on the laptop the
// window one glances at while authoring is the one filling the screen.
//
// AND IT STAYS THAT WAY (architect 2026-08-21): `--fullscreen` here and the
// batch's `fullscreen` property are ONE RULE stated at both entry points, so a
// window that was manually un-fullscreened is fullscreen again at the next
// jump. The spawn sets the opening state and the batch re-asserts it, exactly
// as the rate is re-asserted.
//
// AND NOTHING HERE STYLES THE PLAYER — no OSD, subtitle, volume or profile
// options. mpv reads the user's own ~/.config/mpv, which is where per-user look
// belongs; hardcoding a taste here would override the config silently and give
// the architect no way back.
//
// argv exec, NEVER a shell (the project's standing rule): a project folder's
// name carries spaces and reaches mpv as one argv element with no quoting rules
// in between. `--` guards a dash-leading path.
bool spawn_mpv(const std::string& socket_path, const std::string& video,
               const std::string& start_seconds,
               const std::string& speed) {
    const std::string ipc_arg   = "--input-ipc-server=" + socket_path;
    const std::string start_arg = "--start=" + start_seconds;
    const std::string speed_arg = "--speed=" + speed;
    char* argv[] = {const_cast<char*>("mpv"),
                    const_cast<char*>(ipc_arg.c_str()),
                    const_cast<char*>(start_arg.c_str()),
                    const_cast<char*>(speed_arg.c_str()),
                    const_cast<char*>("--audio-pitch-correction=no"),
                    const_cast<char*>("--fullscreen"),
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
// on the GUI being the writer. sheet.map HAS NO GUI WRITER AT ALL: it is an
// artifact of tools/extract_sheet_map.py, the GUI never saves it, never renders
// from it, and it carries no authored musical content. (It IS committed since
// 2026-08-20 — the one member of `sheet/` that is — and the premises above are
// deliberately narrowed to drop that: being in the repository says nothing
// about who WRITES the file, and the writer is the whole of the argument.) So a
// malformed map is not an attack on the product's data model; it
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
    int64_t prev_section  = 1;
    int64_t prev_measure  = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.front() == '#') {
            // `# src <name>` takes the REST of the line verbatim: a rip's file
            // name carries spaces, so it is not a token. It is a BASENAME by
            // definition (the tool writes os.path.basename) and it is held to
            // that here.
            //
            // WHAT THE CHECK IS FOR, precisely, because the neighbouring fact
            // makes it easy to over-read: it stops the HEADER from doing path
            // traversal. A `# src` carrying a separator or naming `.`/`..`
            // would let a hand-edited map address a file outside the piece's
            // own `sheet/` folder, and the format never means that. IT IS NOT A
            // CONTAINMENT CLAIM ABOUT THE FILE ITSELF: the directory entry may
            // legitimately be a SYMLINK pointing anywhere the user keeps the
            // real video — the deployed layout does exactly this, one real rip
            // with the other movements' folders holding relative symlinks to it
            // — so the open below follows symlinks on purpose and must keep
            // doing so.
            //
            // `sheet.map` IS REFUSED BY NAME: the map's own file name is
            // reserved, so a header naming it is damage rather than a video,
            // and without this line the act would hand mpv the map to play.
            if (line.rfind("# src ", 0) == 0) {
                map.src = line.substr(6);
                if (map.src.empty() || map.src == "." || map.src == ".." ||
                    map.src == "sheet.map" ||
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
                if (!score_video_time_in_domain(start)) return {};
                const std::string_view end_field =
                    std::string_view(rest).substr(sp + 1);
                double end_seconds = 0.0;
                if (end_field != "eof" &&
                    !parse_full_double(end_field, end_seconds))
                    return {};
                map.window_start = start;
                continue;
            }
            // AN UNKNOWN `#` LINE IS SKIPPED, NOT REFUSED, and that tolerance
            // is the format's one forward-compatibility guarantee rather than
            // laziness: the tool may record a field this reader has no use for,
            // and a map is regenerated far less often than the GUI is rebuilt.
            // `# url` (2026-08-20) is its first real instance — pure
            // provenance, written by --url, read by nobody — and it landed
            // needing no change here, which is the guarantee working. A
            // STRUCTURAL line is still judged strictly: only lines this reader
            // CLAIMS are parsed, and a claimed one that is malformed refuses
            // the whole map.
            continue;
        }
        const size_t bar = line.find('|');
        if (bar == std::string::npos) return {};
        double  seconds = 0.0;
        int64_t measure = 0;
        if (!parse_full_double(std::string_view(line).substr(0, bar), seconds))
            return {};
        // THE OPTIONAL `<S>:` QUALIFIER, the measure grammar's own spelling
        // rules applied to a map line (marker_measure.h is the authority):
        // S in [kMeasureMinSection, kMeasureMaxSection], no leading zeros, and
        // `1:` REFUSED because a bare number is section 1's one spelling. A map
        // written before the qualifier existed carries none and reads as one
        // section throughout, which is exactly what it was.
        std::string_view measure_field =
            std::string_view(line).substr(bar + 1);
        int64_t      section = 1;
        const size_t colon   = measure_field.find(':');
        if (colon != std::string_view::npos) {
            if (!parse_canonical_field(measure_field.substr(0, colon), 2,
                                       section) ||
                section < kMeasureMinSection || section > kMeasureMaxSection)
                return {};
            measure_field.remove_prefix(colon + 1);
        }
        if (!parse_canonical_field(measure_field, 5, measure)) return {};
        if (!score_video_time_in_domain(seconds) || measure < 1) return {};
        // TIME RISES STRICTLY ACROSS THE WHOLE MAP; THE MEASURE ONLY WITHIN A
        // SECTION. That split is the printed-number ruling's one structural
        // consequence here, and it is what the lookup's per-section bracketing
        // walk rests on — a new section restarts the measure test rather than
        // failing it, while the time test never restarts. A section may not be
        // re-entered after it is left (the tool emits each section's anchors in
        // one contiguous run), so the previous measure is compared only inside
        // the section that set it.
        if (have_previous && seconds <= prev_seconds) return {};
        if (have_previous && section == prev_section && measure <= prev_measure)
            return {};
        if (have_previous && section < prev_section) return {};
        map.anchors.push_back({seconds, section, measure});
        prev_seconds  = seconds;
        prev_section  = section;
        prev_measure  = measure;
        have_previous = true;
    }
    if (map.anchors.empty()) return {};
    // A MAP WITHOUT `# src` NAMES NO VIDEO and is refused like any other
    // malformed one (2026-08-20). The tool writes that line on EVERY run — it
    // always knows its input — so an absent one is a hand-edited or truncated
    // file rather than an older shape worth tolerating, and refusing here means
    // `map.ok` carries "there is a video to open" for every reader past this
    // point. It is the one header line the format requires.
    if (map.src.empty()) return {};
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

    // ONE VIDEO, ONE RULE (architect 2026-08-20): the map's `# src` basename,
    // opened from the map's OWN FOLDER, seeked to window_start + t. There is no
    // preference order and no fallback to try — the piece's `sheet/` holds one
    // video file under its original name beside `sheet.map`, and that is the
    // whole layout.
    //
    // THE TRIMMED/SOURCE SPLIT IS RETIRED WITH THE TRIMMING. A `sheet.webm` cut
    // to the map's own window was preferred here, with `sheet/src/<name>` behind
    // it for the untrimmed rip; nothing is trimmed any more, so the two names
    // had nothing left to distinguish and the second path was a fallback to a
    // file that no longer exists. WINDOW-RELATIVE TIMES ARE WHY ONE FILE IS
    // ENOUGH: four movements' maps carry four windows into one stable rip, each
    // adding its own offset here.
    //
    // The seek is window_start + t with window_start 0 when the map carries no
    // `# window` — not a fallback but the truth for a whole-video map.
    //
    // SYMLINKS ARE FOLLOWED ON PURPOSE (`is_regular_file`, not the symlink
    // status): the architect's own layout keeps ONE real rip and gives the
    // other movements' `sheet/` folders relative symlinks to it, so refusing to
    // follow would break three pieces out of four. The header's basename guard
    // is what keeps the MAP from naming a path; where that name then points is
    // the user's arrangement of his own disk. A symlink loop fails safely here
    // through the error_code rather than the predicate.
    std::error_code       ec;
    const std::filesystem::path video = project / "sheet" / map.src;
    const double                seek  = map.window_start + t;
    if (!std::filesystem::is_regular_file(video, ec) || ec) return;

    // THE SEEK IS RE-JUDGED AFTER THE SUM, not trusted from its parts: the
    // domain bound above makes an out-of-domain result unreachable, and this
    // asks anyway, because the number is about to be handed to another process
    // as text and a wrong seek is indistinguishable from a right one there.
    // The FORMATTING is judged too — a truncated `%.3f` is a different number,
    // not a shorter one — so a refused spelling is the usual silent no-op.
    if (!score_video_time_in_domain(seek)) return;
    char      seek_text[64];
    const int spelled = std::snprintf(seek_text, sizeof seek_text, "%.3f", seek);
    if (spelled <= 0 || static_cast<size_t>(spelled) >= sizeof seek_text) return;
    const std::string video_path = video.string();
    const std::string socket_path = mpv_socket_path();

    // THE SPEED RIDES THE JUMP (architect ruling 2026-08-20): the score video
    // rolls at the app's own audition rate, so the page you are looking at and
    // the sound you are hearing move together instead of drifting apart within
    // a bar.
    //
    // WHICH RATE IS A VIEW QUESTION. SOURCE view auditions the file itself at
    // `playback_speed`, so that is what mpv takes; TARGET view auditions the
    // rendered preview, which is always played at natural rate — the warp is
    // already baked into those samples — so mpv takes 1.0 there. Reading it
    // here means every jump carries the speed of the MOMENT it was pressed,
    // which is the standing no-live-sync ruling working as intended: nothing
    // follows a later speed change until the next jump.
    //
    // AND PITCH CORRECTION GOES OFF, which is the part that must not be
    // "improved" later. The app's audition is VARISPEED — GuiPlayback fills
    // from a fractional cursor by linear interpolation, so pitch falls with
    // speed exactly as a tape machine's does — and mpv's default is the
    // opposite, holding pitch while it stretches time. Left on, the video would
    // sing a different note than the app at every speed but 1.0. Matching the
    // app means turning the correction OFF, not tuning it.
    const double speed =
        (app.active_audio_view == 'T')
            ? 1.0
            : static_cast<double>(app.playback_speed);
    char      speed_text[64];
    const int speed_spelled =
        std::snprintf(speed_text, sizeof speed_text, "%.6f", speed);
    if (speed_spelled <= 0 ||
        static_cast<size_t>(speed_spelled) >= sizeof speed_text)
        return;

    // FOUR COMMAND LINES ON ONE CONNECTION, written in a single go: mpv's IPC
    // is line-delimited, so this is one write and one round of the bounded
    // transport rather than four. `loadfile` is the jump itself — it loads the
    // file, REPLACES whatever is showing and applies the start option in the
    // same act, so a jump within the same video and a jump into a fresh one are
    // one code path (a same-file jump reloads rather than seeks — accepted, mpv
    // being fast at it and the alternative being a second command shape and a
    // reply to parse). The three PROPERTIES behind it persist in the single
    // instance and are re-sent at every jump anyway, which costs nothing and
    // means the window can never be left holding a stale rate — nor left
    // un-fullscreened, mpv ALWAYS BEING FULLSCREEN for this act (architect
    // 2026-08-21): a manual toggle in the player never survives the next jump,
    // the score being a reading surface every time it is looked at.
    const std::string command = "{\"command\":[\"loadfile\",\"" +
                                json_escape(video_path) +
                                "\",\"replace\",-1,\"start=" + seek_text +
                                "\"]}\n"
                                "{\"command\":[\"set_property\",\"speed\"," +
                                speed_text + "]}\n"
                                "{\"command\":[\"set_property\","
                                "\"audio-pitch-correction\",false]}\n"
                                "{\"command\":[\"set_property\",\"fullscreen\","
                                "true]}\n";
    // THE TWO DIAGNOSTICS SAY WHAT WAS OBSERVED AND NOTHING MORE. No reply is
    // ever read from mpv, so this side cannot know what mpv made of the line —
    // only whether the line left the process. "The write failed" and "the spawn
    // failed" are the two facts in hand, and neither is dressed up as a verdict
    // from the player.
    bool write_failed = false;
    if (send_mpv_command(socket_path, command, write_failed)) return;
    if (write_failed) {
        std::fprintf(
            stderr,
            "warptempo_gui: score video: could not write to the mpv socket\n");
        return;
    }
    // NOTHING WAS LISTENING, so this jump is the one that starts the window.
    if (!spawn_mpv(socket_path, video_path, seek_text, speed_text)) {
        std::fprintf(stderr,
                     "warptempo_gui: score video: could not start mpv\n");
    }
}
