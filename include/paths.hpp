#pragma once

#include <string_view>

namespace paths {
    constexpr std::string_view Root = PROJECT_ROOT;
    constexpr std::string_view TestRoms = TEST_ROMS_PATH;
    constexpr std::string_view Roms = ROMS_PATH;
}
