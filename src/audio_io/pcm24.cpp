#include "pcm24.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

int32_t pcm24_code_from_float(float x)
{
    if (std::isnan(x)) {
        return 0;
    }

    const double scaled = std::clamp(static_cast<double>(x) * 8388608.0,
                                     -8388608.0, 8388607.0);
    return static_cast<int32_t>(std::llrint(scaled));
}

float pcm24_float_from_code(int32_t c)
{
    return static_cast<float>(c) / 8388608.0f;
}

float pcm24_quantize(float x)
{
    return pcm24_float_from_code(pcm24_code_from_float(x));
}
