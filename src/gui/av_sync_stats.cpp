#include "av_sync_stats.h"

#include <cstdio>

// THE PANEL'S TEXT (the contract is at av_sync_stats.h). One composer, three
// groups — the audio half, the display half, and the NET LINE the panel exists
// for — over a monospace face, so the label column lines up by construction
// and no layout term is needed anywhere.

namespace {

// The label column's width in characters. The rows are shaped in the product's
// monospace (paint_handler.cpp's folder-overlay painter selects it under this
// owner), so padding a label to a fixed count of characters IS an aligned
// column.
constexpr int kLabelWidth = 20;

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
    if (!audio.present) {
        rows.push_back(row("Device", "none came up"));
    } else {
        rows.push_back(row("Backend",
                           audio.backend == GuiAudioBackendKind::Jack
                               ? "JACK" : "AAudio"));
        rows.push_back(row("Output rate",
                           fmt_i("%lld Hz",
                                 static_cast<long long>(audio.output_rate))));
        // THE PERIOD WEARS THE BACKEND'S OWN WORD for it, the two being the
        // same quantity under two names.
        rows.push_back(
            row(audio.backend == GuiAudioBackendKind::Jack ? "Buffer size"
                                                           : "Frames per burst",
                fmt_i("%lld frames",
                      static_cast<long long>(audio.period_frames)) +
                    fmt(" (%.2f ms)", ms_of_frames(audio.period_frames,
                                                   audio.output_rate))));
        if (audio.buffer_frames > 0) {
            rows.push_back(
                row("Stream buffer",
                    fmt_i("%lld frames",
                          static_cast<long long>(audio.buffer_frames)) +
                        fmt(" (%.2f ms)", ms_of_frames(audio.buffer_frames,
                                                       audio.output_rate))));
        }
        if (!audio.latency_known) {
            // THE STANDING RECORD, not a gap (playback_aaudio.cpp's Impl): the
            // framework's figures do not carry the Bluetooth link's delay, so
            // a number here would be a guess that is wrong most of the time.
            rows.push_back(row("Output latency", "not reported on this backend"));
        } else if (audio.latency_min_frames == audio.latency_max_frames) {
            rows.push_back(
                row("Output latency",
                    fmt_i("%lld frames",
                          static_cast<long long>(audio.latency_max_frames)) +
                        fmt(" (%.2f ms)",
                            ms_of_frames(audio.latency_max_frames,
                                         audio.output_rate))));
        } else {
            rows.push_back(
                row("Output latency",
                    fmt_i("%lld", static_cast<long long>(
                                      audio.latency_min_frames)) +
                        fmt_i("..%lld frames",
                              static_cast<long long>(audio.latency_max_frames)) +
                        fmt(" (%.2f", ms_of_frames(audio.latency_min_frames,
                                                   audio.output_rate)) +
                        fmt("..%.2f ms)", ms_of_frames(audio.latency_max_frames,
                                                       audio.output_rate))));
        }
    }
    rows.push_back("");

    // -- The display half ----------------------------------------------------
    rows.push_back("Display");
    if (!display.available) {
        rows.push_back(row("Measurement", "not available on this backend"));
    } else {
        rows.push_back(row("Output", display.output_name.empty()
                                         ? std::string("not known yet")
                                         : display.output_name));
        rows.push_back(
            row("Refresh", display.refresh_mhz > 0
                               ? fmt("%.3f Hz",
                                     static_cast<double>(display.refresh_mhz) /
                                         1000.0)
                               : std::string("not known yet")));
        if (!display.instrument_present) {
            rows.push_back(row("Commit to light",
                               "not measured (wp_presentation absent)"));
        } else if (!display.clock_ok) {
            rows.push_back(row("Commit to light", "not measured"));
            rows.push_back(
                row("", "the compositor's clock is not CLOCK_MONOTONIC"));
        } else if (display.sample_count <= 0) {
            rows.push_back(row("Commit to light",
                               "waiting for the first presented frame"));
        } else {
            rows.push_back(row("Commit to light",
                               fmt("mean %.2f ms", ms_of_ns(display.mean_ns))));
            rows.push_back(
                row("", fmt("min %.2f ms, ", ms_of_ns(display.min_ns)) +
                            fmt("max %.2f ms", ms_of_ns(display.max_ns))));
            rows.push_back(
                row("", fmt_i("over %lld presented frames",
                              static_cast<long long>(display.sample_count)) +
                            fmt_i(" of %lld",
                                  static_cast<long long>(display.window))));
        }
    }
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
    // LINE IS EARLY BY L − D, and the phase only ever makes it earlier still.
    // (This is the Android backend's own arithmetic, recorded there since
    // 2026-09-02: "the painted line's error at the moment the pixel lights is
    // L_audio − L_display".)
    rows.push_back("Net");
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
        const double net  = l_ms - d_ms;
        const double phase = ms_of_frames(audio.period_frames,
                                          audio.output_rate);
        rows.push_back(fmt("  The line lights %.2f ms ",
                           net >= 0.0 ? net : -net) +
                       (net >= 0.0 ? "before the sound is heard,"
                                   : "after the sound is heard,"));
        rows.push_back(fmt("  plus the pickup phase 0..%.2f ms, ", phase) +
                       "which moves the line earlier still.");
    }
    return rows;
}
