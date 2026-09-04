#include "av_sync_stats.h"

#include <cassert>
#include <cstddef>
#include <cstdio>

// THE PANEL'S TEXT (the contract is at av_sync_stats.h). One composer, three
// groups — the audio half, the display half, and the NET LINE the panel exists
// for — over a monospace face, so the label column lines up by construction
// and no layout term is needed anywhere.
//
// The shape is fixed and every branch emits the same rows in the same order
// (architect 2026-09-04, on watching the panel relayout the moment the first
// presented frame arrived: it must paint its full shape from the first frame).
// A group whose figure is not measured yet keeps its lines and prints
// `measuring...` with placeholders of the figures' own widths; a group whose
// figure is never coming keeps its lines too and leaves them empty under the
// sentence that says why. So the row count is a constant, a refresh moves only
// text, and the listing's length can no longer change under the reader.

namespace {

// The label column's width in characters. The rows are shaped in the product's
// monospace (paint_handler.cpp's folder-overlay painter selects it under this
// owner), so padding a label to a fixed count of characters IS an aligned
// column.
constexpr int kLabelWidth = 20;

// Each group's fixed height in rows, and the total the composer always yields.
// The three counts are the widest state of each group — the audio half at a
// device that reports everything, the display half at a full presentation
// ring, the net line at a real figure — and every other state pads out to
// them, so the panel's geometry is settled before its first figure lands.
constexpr std::size_t kAudioRowCount   = 5;
constexpr std::size_t kDisplayRowCount = 5;
constexpr std::size_t kNetRowCount     = 3;
constexpr std::size_t kAvSyncRowCount =
    1 +                      // the title
    1 +                      // a blank
    1 + kAudioRowCount +     // "Audio" and its rows
    1 +                      // a blank
    1 + kDisplayRowCount +   // "Display" and its rows
    1 +                      // a blank
    1 + kNetRowCount;        // "Net" and its rows
static_assert(kAvSyncRowCount == 20,
              "the panel's row count is its shape; changing it is a ruling");

// Every figure prints at a fixed character width, zero-padded, so a column
// never shifts as its value grows or as a digit arrives. These widths are the
// panel's contract rather than an incidental format string (architect
// 2026-09-04, his example being `01.00` for a millisecond figure), and each
// spelling that can be printed before its figure exists is paired with a
// placeholder of exactly the same width.
//
// Milliseconds take three integer digits rather than his two, because every
// millisecond column here legitimately passes 100 ms — a large period at
// 44.1 kHz, a port chain feeding a deep sink, or a stalled compositor's worst
// frame in the presentation ring — and a value wider than its column would
// break the alignment the panel is shaped for. One spelling serves all of
// them, so the figures read against each other.
constexpr const char* kMsSpelling       = "%06.2f";  // 021.33
constexpr const char* kMsPlaceholder    = "---.--";
// Frame counts: five digits cover a period, a stream buffer and a port latency
// alike (99999 frames is over two seconds at 44.1 kHz).
constexpr const char* kFramesSpelling   = "%05lld";  // 01024
// Sample rates: six digits cover every rate a device may grant, 192 kHz and
// its doubling included.
constexpr const char* kRateSpelling     = "%06lld";  // 048000
// The refresh in hertz to three decimals; three integer digits cover the
// fastest panel the product will meet.
constexpr const char* kRefreshSpelling  = "%07.3f";  // 059.996
// The presentation ring's two counts, which share one column.
constexpr const char* kCountSpelling    = "%03lld";  // 030
constexpr const char* kCountPlaceholder = "---";

std::string row(const char* label, const std::string& value) {
    std::string s = "  ";
    s += label;
    while (static_cast<int>(s.size()) < 2 + kLabelWidth) s += ' ';
    s += value;
    return s;
}

std::string fmt(const char* format, double v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), format, v);
    return std::string(buf);
}

std::string fmt_i(const char* format, long long v) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), format, v);
    return std::string(buf);
}

double ms_of_frames(int64_t frames, int rate) {
    return rate > 0 ? static_cast<double>(frames) * 1000.0 /
                          static_cast<double>(rate)
                    : 0.0;
}

double ms_of_ns(int64_t ns) { return static_cast<double>(ns) * 1e-6; }

// The four figure spellings as text, each at its column's width.
std::string ms_text(double v) { return fmt(kMsSpelling, v); }
std::string frames_text(int64_t v) {
    return fmt_i(kFramesSpelling, static_cast<long long>(v));
}
std::string rate_text(int v) {
    return fmt_i(kRateSpelling, static_cast<long long>(v));
}
std::string count_text(int v) {
    return fmt_i(kCountSpelling, static_cast<long long>(v));
}

// A count of frames beside its own duration, the audio half's one value shape.
std::string frames_and_ms(int64_t frames, int rate) {
    return frames_text(frames) + " frames (" + ms_text(ms_of_frames(frames,
                                                                   rate)) +
           " ms)";
}

// Fill a group out to its fixed height. The empty rows a short state leaves
// are the honest filler: a line that will never carry a figure says nothing
// rather than showing a placeholder for something that is not coming, and the
// rows below it stay where the reader first saw them.
void pad_to(std::vector<std::string>& rows, std::size_t target) {
    while (rows.size() < target) rows.push_back("");
    assert(rows.size() == target);
}

}  // namespace

std::vector<std::string> compose_av_sync_rows(const GuiAudioStats& audio,
                                              const GuiDisplayStats& display) {
    std::vector<std::string> rows;
    // THE TITLE ROW is the menu item's own words and keeps its Title Case; the
    // dropdown's spelling exception, one surface further in.
    rows.push_back("AV Sync Stats");
    rows.push_back("");

    // -- The audio half ------------------------------------------------------
    rows.push_back("Audio");
    const std::size_t audio_end = rows.size() + kAudioRowCount;
    if (!audio.present) {
        rows.push_back(row("Device", "none came up"));
    } else {
        rows.push_back(row("Backend",
                           audio.backend == GuiAudioBackendKind::Jack
                               ? "JACK" : "AAudio"));
        rows.push_back(row("Output rate", rate_text(audio.output_rate) + " Hz"));
        // THE PERIOD WEARS THE BACKEND'S OWN WORD for it, the two being the
        // same quantity under two names.
        rows.push_back(
            row(audio.backend == GuiAudioBackendKind::Jack ? "Buffer size"
                                                           : "Frames per burst",
                frames_and_ms(audio.period_frames, audio.output_rate)));
        // The second figure's row stands on every backend, and where the
        // backend has no second figure the row says why rather than
        // disappearing. On JACK the period is the whole buffer story and
        // `buffer_frames` is deliberately zero (the producer's own words, at
        // playback.cpp's audio_stats).
        rows.push_back(
            row("Stream buffer",
                audio.buffer_frames > 0
                    ? frames_and_ms(audio.buffer_frames, audio.output_rate)
                    : std::string("the period is the whole buffer")));
        if (!audio.latency_known) {
            // THE STANDING RECORD, not a gap (playback_aaudio.cpp's Impl): the
            // framework's figures do not carry the Bluetooth link's delay, so
            // a number here would be a guess that is wrong most of the time.
            rows.push_back(row("Output latency", "not reported on this backend"));
        } else if (audio.latency_min_frames == audio.latency_max_frames) {
            rows.push_back(row("Output latency",
                               frames_and_ms(audio.latency_max_frames,
                                             audio.output_rate)));
        } else {
            rows.push_back(
                row("Output latency",
                    frames_text(audio.latency_min_frames) + ".." +
                        frames_text(audio.latency_max_frames) + " frames (" +
                        ms_text(ms_of_frames(audio.latency_min_frames,
                                             audio.output_rate)) + ".." +
                        ms_text(ms_of_frames(audio.latency_max_frames,
                                             audio.output_rate)) + " ms)"));
        }
    }
    pad_to(rows, audio_end);
    rows.push_back("");

    // -- The display half ----------------------------------------------------
    rows.push_back("Display");
    const std::size_t display_end = rows.size() + kDisplayRowCount;
    if (!display.available) {
        rows.push_back(row("Measurement", "not available on this backend"));
    } else {
        rows.push_back(row("Output", display.output_name.empty()
                                         ? std::string("not known yet")
                                         : display.output_name));
        rows.push_back(
            row("Refresh", display.refresh_mhz > 0
                               ? fmt(kRefreshSpelling,
                                     static_cast<double>(display.refresh_mhz) /
                                         1000.0) + " Hz"
                               : std::string("not known yet")));
        if (!display.instrument_present) {
            rows.push_back(row("Commit to light",
                               "not measured (wp_presentation absent)"));
        } else if (!display.clock_ok) {
            rows.push_back(row("Commit to light", "not measured"));
            rows.push_back(
                row("", "the compositor's clock is not CLOCK_MONOTONIC"));
        } else if (display.sample_count <= 0) {
            // The ring is turning and its figures are a frame or two away, so
            // these three lines are already the lines they will become: the
            // words say the instrument is working and the placeholders hold
            // each figure's own width until it lands.
            rows.push_back(row("Commit to light", "measuring..."));
            rows.push_back(row("", std::string("min ") + kMsPlaceholder +
                                       " ms, max " + kMsPlaceholder + " ms"));
            rows.push_back(row("", std::string("over ") + kCountPlaceholder +
                                       " presented frames of " +
                                       count_text(display.window)));
        } else {
            rows.push_back(row("Commit to light",
                               "mean " + ms_text(ms_of_ns(display.mean_ns)) +
                                   " ms"));
            rows.push_back(
                row("", "min " + ms_text(ms_of_ns(display.min_ns)) +
                            " ms, max " + ms_text(ms_of_ns(display.max_ns)) +
                            " ms"));
            rows.push_back(
                row("", "over " + count_text(display.sample_count) +
                            " presented frames of " +
                            count_text(display.window)));
        }
    }
    pad_to(rows, display_end);
    rows.push_back("");

    // -- The net line, and the sign is the whole point -----------------------
    //
    // THE DERIVATION. The pre-paint hook samples the predictor and the painter
    // draws the line at the position P the engine has most recently handed to
    // the device; those pixels turn into light D later (the commit-to-light
    // measurement above). The SOUND at that same position P leaves the speaker
    // L ms after the device took it (the output latency above), plus the
    // PICKUP PHASE — a launch publishes at an arbitrary instant inside a
    // period and the first fill happens at that period's end, so the sound
    // starts 0..period behind the predictor's own anchor. So the line showing
    // P lights at (paint + D) while P is heard at (paint + L + phase): THE
    // LINE IS EARLY BY L − D + phase, with phase anywhere in 0..period.
    // (This is the Android backend's own arithmetic, recorded there since
    // 2026-09-02: "the painted line's error at the moment the pixel lights is
    // L_audio − L_display".)
    //
    // SO THE ANSWER IS AN INTERVAL AND NOT A NUMBER: the lead lies in
    // [L − D, L − D + period], and THE SENTENCE SPELLS THAT INTERVAL. It has
    // THREE ARMS because the interval may lie wholly on either side of zero or
    // STRADDLE IT — L − D = −5 ms under a 20 ms period is 5 ms after through
    // 15 ms before, and naming only the low end's sign ("5 ms after") would be
    // the panel failing the one thing it exists to say. THE PERIOD IS THE
    // AUDIO HALF'S OWN FIGURE, the buffer size / frames per burst printed
    // above (GuiAudioStats::period_frames, whose declaration owns the pickup
    // phase's derivation) — nothing new is measured for the net line.
    rows.push_back("Net");
    const std::size_t net_end = rows.size() + kNetRowCount;
    const bool have_audio   = audio.present && audio.latency_known &&
                              audio.output_rate > 0;
    const bool have_display = display.available && display.instrument_present &&
                              display.clock_ok && display.sample_count > 0;
    if (!have_audio && !have_display) {
        rows.push_back("  No net figure: neither the audio output latency");
        rows.push_back("  nor the commit-to-light interval is measured.");
    } else if (!have_audio) {
        rows.push_back("  No net figure: the audio output latency is not");
        rows.push_back("  measured on this backend.");
    } else if (!have_display) {
        rows.push_back("  No net figure: the commit-to-light interval is not");
        rows.push_back("  measured.");
    } else {
        // THE MAX OF THE RANGE is the whole path to the speaker; where the two
        // ends differ (a port feeding sinks of different depth) the shallower
        // one is not what the ear hears last.
        const double l_ms = ms_of_frames(audio.latency_max_frames,
                                         audio.output_rate);
        const double d_ms = ms_of_ns(display.mean_ns);
        const double phase = ms_of_frames(audio.period_frames,
                                          audio.output_rate);
        // THE INTERVAL, in lead: `lo` is the pickup phase at zero and `hi` is
        // the phase at a whole period, so lo <= hi always and the sign of each
        // end picks the arm. Positive is EARLY (the line ahead of the sound).
        const double lo = l_ms - d_ms;
        const double hi = lo + phase;
        if (lo >= 0.0) {
            rows.push_back("  The line lights between " + ms_text(lo) +
                           " and " + ms_text(hi) + " ms before");
            rows.push_back("  the sound is heard.");
        } else if (hi <= 0.0) {
            rows.push_back("  The line lights between " + ms_text(-hi) +
                           " and " + ms_text(-lo) + " ms after");
            rows.push_back("  the sound is heard.");
        } else {
            rows.push_back("  The line lights between " + ms_text(-lo) +
                           " ms after and");
            rows.push_back("  " + ms_text(hi) +
                           " ms before the sound is heard.");
        }
        rows.push_back("  The spread is the launch's pickup phase, 0.." +
                       ms_text(phase) + " ms.");
    }
    pad_to(rows, net_end);
    assert(rows.size() == kAvSyncRowCount);
    return rows;
}
