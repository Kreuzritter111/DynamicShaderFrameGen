#include "RenderPolicy.h"
#include <iostream>
#include <limits>

int main()
{
    using namespace FrameGen::RenderPolicy;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        if (!ok) { std::cerr << name << '\n'; ++failures; }
    };
    for (float requested : {0.0f, 0.3f, 0.667f, 1.0f,
             std::numeric_limits<float>::quiet_NaN()}) {
        check(Scale(true, 0, requested) == 1.0f, "DLAA silently reduced native resolution");
        check(Scale(false, 1, requested) == 1.0f, "Disabled upscaler reduced resolution");
    }
    check(Scale(true, 1, 0) == 2.0f / 3.0f, "Quality mode ratio");
    check(Scale(true, 3, 0.01f) == 0.3f, "Custom ratio minimum");
    check(Scale(true, 2, 2.0f) == 1.0f, "Custom ratio maximum");
    for (int provider : {0, 1}) {
        check(!UseDlssg(false, provider), "FG off must not select DLSSG");
        check(!UseFsr(false, provider), "FG off must not create FSR context");
    }
    check(UseDlssg(true, 1) && !UseFsr(true, 1), "NVIDIA provider");
    check(UseFsr(true, 0) && !UseDlssg(true, 0), "AMD provider");
    if (!failures) std::cout << "Render policy: all checks passed\n";
    return failures ? 1 : 0;
}
