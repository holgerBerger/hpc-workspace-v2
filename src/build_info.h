#pragma once

#include <fmt/base.h>

#include "caps.h"

extern Cap caps;

namespace utils {

// helper to print build flags
void printBuildFlags() {
    bool userdebug = false;
    bool capa = false;

#ifdef WS_ALLOW_USER_DEBUG
    userdebug = true;
#endif
#ifdef WS_CAPA
    capa = true;
#endif
    fmt::println("Build flags: WS_CAPA={}, WS_ALLOW_USER_DEBUG={}", capa, userdebug);
    fmt::println("Runtime flags: isusermode={}, issetuid={}, hascaps={}", caps.isUserMode(), caps.isSetuid(),
                 caps.hasCaps());
}

} // namespace utils
