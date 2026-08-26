#include "playback.h"

// THE SILENT PLAYBACK DEVICE — the Android arm of playback.h until M4 brings
// the AAudio backend, and the one thing in the port that answers a contract
// without honoring it. It satisfies every declaration in playback.h so the GUI
// compiles and runs whole on a platform that has no JACK; nothing here makes a
// sound, and no call site knows.
//
// THIS FILE IS NOT IN THE LINUX TARGET: exactly one of playback.cpp (JACK) and
// this file is compiled into a given binary, the same one-arm-per-backend rule
// gui_font_fontconfig.cpp / gui_font_bundled.cpp follow.
//
// WHAT IT ANSWERS, AND WHY THOSE ANSWERS. The GUI reads playback state at
// every frame (the scanner, the transport row's glyph, the follow chase, the
// A/B audition's own running bit), so the stub's answers must be the answers a
// device that is not playing would give rather than zeros:
//   * init()        -> true. A false would be read as "the audio device failed"
//                     and print the GUI's own startup error, which would be a
//                     lie about a device that was never asked for. The bind is
//                     recorded so the domain accessors below can be honest.
//   * play()        -> the cursor lands at the play START and stays there.
//                     is_playing() stays false, so every consumer sees a
//                     transport that never started rather than one that ended
//                     — no natural-end teardown fires, no scanner runs, and
//                     NOTHING LOOPS holds trivially.
//   * domain_*      -> the bound buffer's real extent. These are RANGE POLICY
//                     for call sites (they clamp play windows against them),
//                     not audio facts, so they must be right even with no
//                     device: an answer of 0..0 would silently collapse every
//                     range that consults them.
//   * everything else is a no-op with no state to keep.
//
// M4 REPLACES THIS FILE, not the header: the AAudio backend implements the
// same Impl-behind-a-unique_ptr shape playback.cpp does.

#include <cstdint>

struct GuiPlayback::Impl {
    // The bound buffer's EXTENT, recorded so the domain accessors answer from
    // the same truth playback.cpp answers from. The buffer POINTER is
    // deliberately not kept: nothing here ever reads a sample, and a field
    // nothing reads is a field that goes stale unnoticed.
    int64_t      total_frames  = 0;
    int64_t      domain_offset = 0;
    // The last position play() was asked for, in domain coordinates — the
    // whole of "where the cursor is" on a device that never advances it.
    int64_t      cursor        = 0;
};

GuiPlayback::GuiPlayback() : impl_(new Impl) {}
GuiPlayback::~GuiPlayback() = default;

bool GuiPlayback::init(int /*sample_rate*/, int /*channels*/,
                       const float* /*samples*/, int64_t total_frames,
                       int64_t domain_offset) {
    impl_->total_frames  = total_frames;
    impl_->domain_offset = domain_offset;
    impl_->cursor        = domain_offset;
    return true;
}

void GuiPlayback::play(int64_t start_sample, int64_t /*end_sample*/) {
    // The cursor lands where the play was asked to begin and stays: a paused
    // transport parked at the requested instant is the honest picture of a
    // device that cannot advance it.
    impl_->cursor = start_sample;
}

void GuiPlayback::stop() {}

void GuiPlayback::set_speed(float /*speed*/) {}

void GuiPlayback::resync_predictor() {}

bool GuiPlayback::is_playing() const { return false; }

int64_t GuiPlayback::cursor() const { return impl_->cursor; }

double GuiPlayback::cursor_precise() const {
    return static_cast<double>(impl_->cursor);
}

int64_t GuiPlayback::domain_begin() const { return impl_->domain_offset; }

int64_t GuiPlayback::domain_end() const {
    return impl_->domain_offset + impl_->total_frames;
}

void GuiPlayback::rebind_buffer(const float* /*samples*/, int64_t total_frames,
                                int64_t domain_offset) {
    impl_->total_frames  = total_frames;
    impl_->domain_offset = domain_offset;
    // playback.cpp resets the cursor at a rebind; the same reset here keeps a
    // position from one binding out of another binding's domain.
    impl_->cursor        = domain_offset;
}

void GuiPlayback::shutdown() {
    impl_->total_frames = 0;
}
