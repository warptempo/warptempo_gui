#pragma once
#include <cstdint>
#include <string>
#include <vector>

// THE AV SYNC PANEL'S TWO READINGS AND THE LINES THEY COMPOSE INTO (architect
// 2026-09-03, Help → AV Sync Stats). The panel answers ONE question — by how
// much does the painted playhead lead or trail the sound it names — and it
// answers it out of two figures that live on opposite sides of the product:
// the audio device's OUTPUT LATENCY (GuiPlayback::audio_stats, one arm per
// backend) and the display's COMMIT-TO-LIGHT interval (GuiPlatform::
// display_stats, one arm per backend). Both types are plain values declared
// here, gui_media.h's own shape, so the two GuiPlatform headers, the two
// playback backends and the panel's opener share ONE spelling.
//
// THE MEASUREMENTS RUN ONLY WHILE THE PANEL STANDS (the architect's ruling of
// the same day: "in all cases the measurements should be disabled when not
// explicitly requested by the user"). Nothing here is cached, polled or
// published on its own: the two getters are asked once per frame BY THE PANEL,
// and the display half's instrument is armed and disarmed by the panel's open
// and close through GuiPlatform::set_display_measurement. With the panel down
// no feedback object exists, no ring turns and no port latency is read. A
// LATENCY INSTRUMENT LIVED ON THE AUDIO BACKEND FOR TWO DAYS and was deleted
// on 2026-09-03 with the playback leads it fed (playback.cpp's head): its
// shape was a CALLBACK-DRIVEN CACHED ATOMIC, and this is deliberately not
// that — the read is a plain query the panel makes while it is up.

enum class GuiAudioBackendKind { Jack, AAudio };

// WHAT THE AUDIO DEVICE SAYS ABOUT ITSELF, read on demand on the main thread.
// `present` false means no device came up at all, and every other field is
// then meaningless — the panel says so on one line rather than printing zeros
// as if they were measurements.
struct GuiAudioStats {
    bool                present       = false;
    GuiAudioBackendKind backend       = GuiAudioBackendKind::Jack;
    // The graph's rate (JACK) or the granted stream rate (AAudio), in Hz.
    int                 output_rate   = 0;
    // THE PERIOD: the block the device asks for at a time — JACK's buffer
    // size, AAudio's frames per burst. It is the PICKUP PHASE'S span: a launch
    // publishes at an arbitrary instant inside one of these and the first fill
    // happens at its end, so the sound starts 0..period after the predictor's
    // own anchor.
    int                 period_frames = 0;
    // A SECOND FIGURE WHERE THE BACKEND HAS ONE (AAudio's stream buffer size,
    // which is the burst times the number of bursts it was given); 0 where the
    // period is the whole story, as it is on JACK.
    int                 buffer_frames = 0;
    // THE OUTPUT LATENCY, in frames — how long after the engine hands a frame
    // to the device that frame is heard. `latency_known` false is the honest
    // answer on a backend whose framework does not report a trustworthy figure
    // (AAudio: the car's Bluetooth route is large, variable and unreported),
    // and the panel then prints no net line rather than a wrong one.
    bool                latency_known      = false;
    int64_t             latency_min_frames = 0;
    int64_t             latency_max_frames = 0;
};

// WHAT THE DISPLAY SAYS ABOUT ITSELF. `available` false is a backend that
// cannot measure at all (Android — no feedback road on the lock/unlockAndPost
// path), and the panel says which rather than guessing.
struct GuiDisplayStats {
    bool        available          = false;
    // The window's own output (the selection rule at GuiPlatform's `outputs_`)
    // and its current mode, in millihertz; an empty name and a zero refresh
    // are "not known yet" and print as such.
    std::string output_name;
    int         refresh_mhz        = 0;
    // THE INSTRUMENT'S TWO PRECONDITIONS, reported so an absent figure names
    // its own reason: the protocol global was advertised, and the clock it
    // timestamps in is the predictor's own (CLOCK_MONOTONIC). A lead measured
    // across two clocks is a number, not a measurement.
    bool        instrument_present = false;
    bool        clock_ok           = false;
    // THE RING'S READING: how many presented frames it holds, out of `window`,
    // and the mean, min and max of (presented − sampled) over them.
    int         sample_count       = 0;
    int         window             = 0;
    int64_t     mean_ns            = 0;
    int64_t     min_ns             = 0;
    int64_t     max_ns             = 0;
};

// THE PANEL'S ROWS, in painted order — the ONE place the two readings become
// text, so the panel is one function of two structs. Each string is one row of
// the folder overlay's Text kind; an empty string is a blank spacing row.
// Sentence case on every line but the title, and no key hints anywhere (the
// standing rules; the text convention's owner is paint_handler.cpp's
// capitalization block).
//
// The row count is the same in every state, and that is the panel's shape
// (architect 2026-09-04): the composer emits one fixed set of lines whatever
// the two readings say, so the listing can never change length under the
// reader and a refresh repaints text alone. A figure still being measured
// prints `measuring...` with placeholders at its own columns' widths, a figure
// that is never coming leaves its line empty under the sentence that says why,
// and every number is zero-padded to a width its column owns. The composer's
// own comments carry the counts and the widths.
std::vector<std::string> compose_av_sync_rows(const GuiAudioStats& audio,
                                              const GuiDisplayStats& display);
