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
// hook and never fires it, and its publish is a no-op. The laptop has no head
// unit at all, so nothing on it produces one of these.
//
// A COMMAND IS THE CAR'S OWN ACT ON THE PLAYER (architect 2026-09-12, from the
// car: "the car is a separate interface"). Every kind runs one of
// GuiRenderPlayer's own bodies direct (on_media_command, render_player.h);
// nothing is synthesized and nothing is dispatched. THE CAR'S VOCABULARY IS
// THE CAR'S AND NOT THE TABLET'S KEYS: the wheel and the console carry rewind,
// play/pause and fast-forward, the outer two arriving as Previous and Next, so
// the one button is a TOGGLE between the item and silence and the outer two
// are the PLAYLIST WALK — a row of the listing at a rest, a file of the
// playing folder while live — with the up-a-folder exit on Previous alone.
// EVERY COMMAND PRESSED ONE OF THE PLAYER'S KEYS until that day (Space, Home /
// End, Left / Right through GuiPlatform::synthesize_key, with a stable-code
// base of its own), and the press road CLEARED THE MODAL RING first, because a
// synthesized Space on a ring-focused button is that button's press — Close,
// and the player would come down. A direct act presses no button, so the road,
// the base and the clear are all gone. The table at on_media_command owns each
// arm's reason.

struct GuiMediaCommand {
    // THE KIND TABLE IS SHARED WITH THE JAVA SLIVER BY NUMBER: each enumerator's
    // integer value is the `MEDIA_*` constant MainActivity.java hands
    // nativeMediaCommand, in this order, 0-based. THE TWO TABLES ARE ONE LIST
    // AND ARE EDITED IN ONE ACT — the numbers are an in-build identity and
    // nothing persists them, so a kind lands where it belongs rather than
    // always at the end; kGuiMediaCommandKindCount below moves with the list
    // and the static_assert under it is what keeps the two the same length.
    //
    // THE TOGGLE HAS ITS OWN KIND because the sliver MAPS THE KEYCODES ITSELF
    // (MainActivity's onMediaButtonEvent override): the framework's default
    // would split KEYCODE_MEDIA_PLAY_PAUSE against the session's published
    // state — after holding the press for a double-tap window it turns into a
    // skip — so the sliver bypasses it and hands the undivided key down as
    // PlayPause. HEADSETHOOK arrives as the same kind. THE THREE PLAY/PAUSE
    // KINDS NOW MEAN ONE THING: the published state says PLAYING whenever the
    // player stands, so a console sends whichever verb its own display
    // believes and all three reach the player's one toggle.
    enum class Kind : int {
        Play              = 0,   // MEDIA_PLAY
        Pause             = 1,   // MEDIA_PAUSE
        PlayPause         = 2,   // MEDIA_PLAY_PAUSE (the undivided toggle key)
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
    Kind    kind        = Kind::Play;
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
    // buttons reach nothing. SINCE 2026-09-12 IT ALSO SAYS WHAT THE STATE IS:
    // the consuming side publishes PLAYING at speed 1.0 whenever this is true
    // and STOPPED when it is not, with no third answer, because a console
    // reads the still-streaming Bluetooth link as playing and OVERRIDES a
    // session that says PAUSED — so the session tells it what it already
    // believes and its one button becomes a plain toggle (the ruling and its
    // reasons are at GuiRenderPlayer::publish_media_state).
    bool        session_active = false;
    // THE TRUE TRANSPORT BIT — the player's item is sounding — and it is read
    // on the consuming side FOR THE AUDIO FOCUS ALONE since 2026-09-12: the
    // published state is `session_active`'s above. It is the tablet's truth,
    // not the console's picture.
    bool        playing        = false;
    // THE THREE STRINGS ARE THE PROJECT, THE FOLDER AND THE NAME (architect
    // 2026-09-12, from the car, on the console's own picture: it lays the
    // metadata out as ALBUM above the title, dim, and ARTIST below it — the
    // opposite of the pairing's first try, which could not be told apart
    // while both lines read the project's name): the dim top line is the
    // LEAST important of the three, so it carries the project, and the
    // bottom line carries the folder. The one function of (transport, item,
    // highlight) that fills all three is stated at
    // GuiRenderPlayer::publish_media_state and read nowhere else.
    //
    // THE BARE NAME, no path at all: the ITEM's file name while it sounds
    // (`01.wav`), and with NOTHING SOUNDING THE HIGHLIGHTED ROW's own name —
    // a wav's or a folder's, and a folder's WITHOUT a trailing slash, the
    // artist beneath it already saying where the listener is — which is what
    // the console's own button would start. That second arm is THE SILENCE
    // TRACK, what plays while the listener is at the top level walking
    // folders; with nothing to highlight at all the LISTED FOLDER names
    // itself, the same word the artist carries. IT IS NEVER EMPTY WHILE
    // `session_active` IS TRUE, and that is the point rather than a tidiness:
    // a console handed a session with nothing in it goes back to its own idle
    // picture. The title is empty exactly at the close's inactive push. THE
    // SILENCE IS METADATA AND NEVER A FILE — a silent wav on disk would be
    // listed by the player, mirrored by Synchronize and played by the
    // auto-advance.
    std::string title;
    // THE FOLDER THE NAME LIVES IN, bare: the PLAYING ITEM'S OWN folder while
    // it sounds — the band may have walked somewhere else, and the sounding
    // item's home is the truth — and otherwise the folder the band is in,
    // `tmp` at the root and the batch folder's own name inside one. The
    // player has lived inside `tmp/` since 2026-09-01, so `render/` is not a
    // spelling this carries. Never empty while `session_active` is true; the
    // close's inactive push is the one empty artist, as it is the one empty
    // title. THE CONSOLE'S BOTTOM LINE.
    std::string artist;
    // The project's name, always — the console's DIM TOP LINE, above the
    // title, and the one string that does not move while the player is
    // walked.
    std::string album;
    // THE ITEM'S LENGTH IN MILLISECONDS, OR -1 FOR UNKNOWN — which is what the
    // SILENCE TRACK sends, the consuming side putting no duration key at all
    // for a value of 0 or less (Android's "unknown"): the state says PLAYING
    // at speed 1.0, so a real duration would run the console's clock into a
    // track end that never comes. The item's own arm carries its real length.
    int64_t     duration_ms    = 0;
    int64_t     position_ms    = 0;
};
