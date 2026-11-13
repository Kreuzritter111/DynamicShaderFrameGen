#pragma once

#include <algorithm>
#include <cmath>

namespace FrameGen::RenderPolicy
{
    // DLAA is always native. Disabled upscaling must never lower render size.
    inline float Scale(bool upscale, int quality, float requested)
    {
        if (!upscale || quality == 0)
            return 1.0f;
        constexpr float ratios[]{1.0f, 2.0f / 3.0f, 0.585f, 0.5f, 1.0f / 3.0f};
        if (std::isfinite(requested) && requested > 0.0f)
            return std::clamp(requested, 0.30f, 1.0f);
        return ratios[std::clamp(quality, 0, 4)];
    }

    constexpr bool UseDlssg(bool frameGeneration, int provider)
    {
        return frameGeneration && provider == 1;
    }

    constexpr bool UseFsr(bool frameGeneration, int provider)
    {
        return frameGeneration && provider == 0;
    }
}
