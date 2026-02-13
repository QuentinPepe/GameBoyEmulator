#pragma once

#include <SDL.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class EmuSystem { GameBoy, GameBoyAdvance, PlayStation1 };

struct RomEntry {
    std::filesystem::path Path;
    std::string Title;
    std::string Filename;
};

std::optional<EmuSystem> SelectSystem(SDL_Renderer* renderer);

std::vector<RomEntry> ScanRoms(const std::filesystem::path& dir, EmuSystem system);

std::optional<std::filesystem::path> SelectRom(
    SDL_Renderer* renderer,
    const std::vector<RomEntry>& roms,
    const char* header = "PHOSPHOR");

void ShowEmptyRomList(SDL_Renderer* renderer, const char* header, const char* romDir);
