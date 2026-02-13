#include <SDL.h>
#include <print>
#include <filesystem>
#include <string>
#include <algorithm>

#include <rom_selector.hpp>
#include <gb_run.hpp>

static bool IsGameBoyRom(const std::string& ext)
{
    return ext == ".gb" || ext == ".gbc";
}

static const char* SystemHeader(EmuSystem system)
{
    switch (system)
    {
    case EmuSystem::GameBoy: return "GAME BOY";
    case EmuSystem::PS1:     return "PLAYSTATION";
    }
    return "PHOSPHOR";
}

static std::filesystem::path SystemRomDir(EmuSystem system)
{
    switch (system)
    {
    case EmuSystem::GameBoy: return "roms/gameboy";
    case EmuSystem::PS1:     return "roms/ps1";
    }
    return "roms";
}

static std::filesystem::path FindProjectRoot()
{
    if (std::filesystem::is_directory("roms")) return ".";

    char* basePath = SDL_GetBasePath();
    if (basePath)
    {
        auto dir = std::filesystem::path(basePath);
        SDL_free(basePath);

        for (int i = 0; i < 5; i++)
        {
            if (std::filesystem::is_directory(dir / "roms")) return dir;
            auto parent = dir.parent_path();
            if (parent == dir) break;
            dir = parent;
        }
    }

    return ".";
}

int main(int argc, char* argv[])
{
    std::println("Phosphor v0.2.0");
    std::println("==================\n");

    bool startFullscreen = false;
    bool runTests = false;
    std::string argPath;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--fullscreen" || arg == "-f")
            startFullscreen = true;
        else if (arg == "--test")
            runTests = true;
        else
            argPath = arg;
    }

    if (runTests)
    {
        auto testDir = argPath.empty()
            ? (std::filesystem::path(SDL_GetBasePath()) / "../../../test-roms").string()
            : argPath;
        gameboy::RunTests(testDir);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0)
    {
        std::println(stderr, "SDL init failed: {}", SDL_GetError());
        return 1;
    }

    if (!argPath.empty() && !std::filesystem::is_directory(argPath))
    {
        auto ext = std::filesystem::path(argPath).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        int result;
        if (IsGameBoyRom(ext))
            result = gameboy::Run(argPath, startFullscreen);
        else
        {
            std::println(stderr, "Unsupported file: {}", argPath);
            result = 1;
        }
        SDL_Quit();
        return result;
    }

    auto baseDir = argPath.empty() ? FindProjectRoot() : std::filesystem::path(argPath);

    SDL_Window* w = SDL_CreateWindow("Phosphor",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 576, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    SDL_Renderer* r = SDL_CreateRenderer(w, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    if (startFullscreen) SDL_SetWindowFullscreen(w, SDL_WINDOW_FULLSCREEN_DESKTOP);

    int result = 0;
    bool launched = false;

    while (!launched)
    {
        auto system = SelectSystem(r);
        if (!system) break;

        auto scanDir = baseDir / SystemRomDir(*system);
        auto roms = ScanRoms(scanDir, *system);
        if (roms.empty())
        {
            std::println("No ROMs found in: {}", scanDir.string());
            continue;
        }

        auto selected = SelectRom(r, roms, SystemHeader(*system));
        if (!selected) continue;

        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);

        switch (*system)
        {
        case EmuSystem::GameBoy:
            result = gameboy::Run(selected->string(), startFullscreen);
            break;
        default:
            std::println(stderr, "System not yet implemented");
            result = 1;
            break;
        }
        launched = true;
    }

    if (!launched)
    {
        SDL_DestroyRenderer(r);
        SDL_DestroyWindow(w);
    }

    SDL_Quit();
    return result;
}
