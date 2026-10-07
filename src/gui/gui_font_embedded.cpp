#include "gui_font.h"

// THE LINUX BINARY CARRIES ITS OWN FACES (architect 2026-10-02, "bundle it for
// both"): the four files under the repository's `fonts/` (gui_font.h's
// kGuiFontFiles) are compiled into the executable here, so the laptop depends
// on no installed font package and asks fontconfig nothing. The Wayland
// backend hands these to gui_font_install_bundled (gui_font.h) once, at the
// head of GuiPlatform::init. THIS FILE IS NOT IN THE ANDROID TARGET (the APK
// ships the same files as assets, which its backend reads out of the
// package), nor in warptempo_cli, which paints nothing.
//
// THE MECHANISM: each `.inc` is the font file's bytes as a comma-separated
// list of hex literals, written into the build tree at CONFIGURE time by the
// Linux target's font step (CMakeLists.txt) with nothing but CMake's own
// file(READ … HEX), the configure re-running whenever a font file changes.
// Chosen over C++26's #embed, which would have been the shortest road,
// because #embed needs GCC 15 or Clang 19 where the documented floor is
// GCC 12 / Clang 16 (docs/INSTALL.md); and over `ld -r -b binary`, whose
// linker-made symbols carry path-derived names and section flags to police.
// The includes are ordinary headers to the compiler, so a font swap rebuilds
// this one object and nothing else.
//
// THE ARRAYS ARE THE FILES, BYTE FOR BYTE, and nothing is appended:
// FreeType is handed an explicit length, so no terminator is needed, and
// gui_font_install_bundled copies the bytes it keeps.

// ONE ARRAY PER FILE, IN kGuiFontFiles' ORDER: the font step writes an
// `.inc` for every name that list spells, and the names here follow it by
// hand — a file added to the list and not here fails the static_assert's
// count, a file named here and not there fails the include.
namespace {

const uint8_t kTahoma[] = {
#include "tahoma.ttf.inc"
};
const uint8_t kTahomaBold[] = {
#include "tahomabd.ttf.inc"
};
const uint8_t kDejaVuSans[] = {
#include "DejaVuSans.ttf.inc"
};
const uint8_t kDejaVuSansBold[] = {
#include "DejaVuSans-Bold.ttf.inc"
};

} // namespace

static_assert(kGuiFontFileCount == 4);
const GuiFontBytes gui_font_embedded_files[kGuiFontFileCount] = {
    {kTahoma, sizeof(kTahoma)},
    {kTahomaBold, sizeof(kTahomaBold)},
    {kDejaVuSans, sizeof(kDejaVuSans)},
    {kDejaVuSansBold, sizeof(kDejaVuSansBold)},
};
