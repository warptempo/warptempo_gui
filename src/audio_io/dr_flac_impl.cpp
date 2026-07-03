// Single-TU implementation of dr_flac. The header is vendored in full; every
// other consumer includes it without implementation or policy macros. CMake
// suppresses warnings on this third-party C implementation TU only.
//
// App policy is native FLAC only (audio_probe dispatches on the fLaC magic), so
// the Ogg/FLAC container path is unreachable; compile it out. The wide-character
// open API is unused on this Linux-only target. Every DR_FLAC_NO_OGG /
// DR_FLAC_NO_WCHAR guard in the header sits in the implementation section, so
// declarations and struct layouts are unchanged and other TUs may include the
// header without these macros. Never define DR_FLAC_NO_CRC: frame CRC checking
// is load-bearing corruption detection.
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
