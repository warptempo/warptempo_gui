// Single-TU implementation of dr_flac. The header is vendored in full; every
// other consumer includes it without the implementation macro. CMake suppresses
// warnings on this third-party C implementation TU only.
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
