#pragma once

#include <string>
#include <types.hpp>

namespace gb {
    S32 Run(const std::string& romPath, bool fullscreen);
    void RunTests(const std::string& testRomsDir);
}
