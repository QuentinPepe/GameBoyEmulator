#pragma once

#include <string>

namespace gameboy {
    int Run(const std::string& romPath, bool fullscreen);
    void RunTests(const std::string& testRomsDir);
}
