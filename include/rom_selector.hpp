#pragma once

#include <SDL.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct RomEntry {
    std::filesystem::path Path;
    std::string Title;     // From ROM header (0x0134-0x0143)
    std::string Filename;  // Just the filename
};

std::vector<RomEntry> ScanRoms(const std::filesystem::path& dir);

std::optional<std::filesystem::path> SelectRom(
    SDL_Renderer* renderer,
    const std::vector<RomEntry>& roms);
