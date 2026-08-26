#pragma once

// The platform seam's ONE include: the backend class is GuiPlatform in every
// build, its header chosen here and nowhere else. A consumer includes this and
// never a backend header, so adding a backend touches this file alone.
#ifdef __ANDROID__
#include "platform_android.h"
#else
#include "platform_wayland.h"
#endif
