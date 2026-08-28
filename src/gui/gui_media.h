#pragma once
#include <cstdint>
#include <string>

// THE CAR'S VOCABULARY ACROSS THE SEAM (architect design 2026-08-28 §3): what a
// head unit's buttons say to the product, and what the product says back for
// the head unit's display. Both types are the seam's — declared here so the
// two GuiPlatform headers and the render player share ONE spelling — and both
// are plain values: the backend that has a MediaSession (Android) fills the
// first from its callbacks and consumes the second into the session's
// metadata and playback state; the backend that has none (Wayland) stores the
// hook and never fires it, and its publish is a no-op. On the laptop the car
// keys are simply the keyboard.
//
// A COMMAND IS TRANSLATED INTO THE PLAYER'S OWN KEYS, never dispatched on its
// own road (GuiRenderPlayer::on_media_command, render_player.h): every car
// button is a chord the player already binds, so the ordinary on_key dispatch
// runs and the mode's refusals, its gesture-modal swallow and its ring hold
// exactly as for a key. The one exception is SeekTo, recorded there.

struct GuiMediaCommand {
    // THE KIND TABLE IS SHARED WITH THE JAVA SLIVER BY NUMBER: each enumerator's
    // integer value is the `MEDIA_*` constant MainActivity.java hands
    // nativeMediaCommand, in this order, 0-based. A new kind is added at the
    // END on both sides, and kGuiMediaCommandKindCount below moves with it —
    // the static_assert under it is what keeps the two tables the same length.
    enum class Kind : int {
        Play              = 0,   // MEDIA_PLAY
        Pause             = 1,   // MEDIA_PAUSE
        PlayPause         = 2,   // MEDIA_PLAY_PAUSE (no Android producer today:
                                 // the framework's default media-button routing
                                 // splits the toggle key into Play / Pause by the
                                 // session's PUBLISHED state; kept as the seam's
                                 // natural toggle for a backend that delivers one)
        Stop              = 3,   // MEDIA_STOP
        Next              = 4,   // MEDIA_NEXT
        Previous          = 5,   // MEDIA_PREVIOUS
        FastForward       = 6,   // MEDIA_FAST_FORWARD
        Rewind            = 7,   // MEDIA_REWIND
        SeekTo            = 8,   // MEDIA_SEEK_TO (position_ms carries the target)
        FocusLost         = 9,   // MEDIA_FOCUS_LOST (AUDIOFOCUS_LOSS)
        FocusLostTransient = 10, // MEDIA_FOCUS_LOST_TRANSIENT (AUDIOFOCUS_LOSS_TRANSIENT*)
        FocusGained       = 11,  // MEDIA_FOCUS_GAINED (AUDIOFOCUS_GAIN)
    };
    Kind    kind        = Kind::PlayPause;
    // Milliseconds into the item; read for SeekTo alone, 0 otherwise.
    int64_t position_ms = 0;
};

// THE COUNT THE JAVA TABLE MUST MATCH (MainActivity.java's MEDIA_KIND_COUNT).
// The JNI entry drops any integer outside [0, count) rather than casting it.
inline constexpr int kGuiMediaCommandKindCount = 12;
static_assert(static_cast<int>(GuiMediaCommand::Kind::FocusGained) + 1 ==
                  kGuiMediaCommandKindCount,
              "the media command kind table and its count have drifted");

// WHAT THE HEAD UNIT SHOWS — pushed by the ONE owner
// GuiRenderPlayer::publish_media_state at every edge where it changes (the
// inventory is at that function), never per tick: a playing state advances
// on the head unit's own clock from the last push at speed 1.0, which is what
// a media session's (state, position, speed) triple means.
struct GuiMediaState {
    // The session is active exactly while the render player stands (the
    // design's R7); inactive is the close's push, after which the head unit's
    // buttons reach nothing.
    bool        session_active = false;
    bool        playing        = false;
    // The wav's spelling with its folder, relative to the project
    // (`tmp/3_bpm/01.wav`, `render/<title>.wav`); empty with no item.
    std::string title;
    // The project's name; the album is the artist on the consuming side.
    std::string artist;
    int64_t     duration_ms    = 0;
    int64_t     position_ms    = 0;
};
