#include <SDL.h>
#include <print>
#include <format>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <paths.hpp>
#include <gameboy.hpp>
#include <ppu.hpp>
#include <apu.hpp>
#include <joypad.hpp>

void RunTests()
{
    const std::vector<std::string> tests = {
        "cpu_instrs/individual/01-special.gb",
        "cpu_instrs/individual/02-interrupts.gb",
        "cpu_instrs/individual/03-op sp,hl.gb",
        "cpu_instrs/individual/04-op r,imm.gb",
        "cpu_instrs/individual/05-op rp.gb",
        "cpu_instrs/individual/06-ld r,r.gb",
        "cpu_instrs/individual/07-jr,jp,call,ret,rst.gb",
        "cpu_instrs/individual/08-misc instrs.gb",
        "cpu_instrs/individual/09-op r,r.gb",
        "cpu_instrs/individual/10-bit ops.gb",
        "cpu_instrs/individual/11-op a,(hl).gb",
        "instr_timing/instr_timing.gb",
        "mem_timing/individual/01-read_timing.gb",
        "mem_timing/individual/02-write_timing.gb",
        "mem_timing/individual/03-modify_timing.gb",
        "mem_timing/mem_timing.gb",
    };

    int passed = 0, failed = 0;

    for (const auto& test : tests)
    {
        auto romPath = std::format("{}/{}", paths::TestRoms, test);
        auto cart = Cartridge::Load(romPath);
        if (!cart)
        {
            std::println("{}: SKIP", test);
            continue;
        }

        GameBoy gb{std::move(*cart)};

        U32 cycles = 0;
        constexpr U32 maxCycles = 200'000'000;

        while (gb.GetBus().GetTestResult() == TestResult::Running && cycles < maxCycles)
        {
            cycles += gb.Step();
        }

        if (gb.GetBus().GetTestResult() == TestResult::Passed)
        {
            std::println("{}: PASSED", test);
            ++passed;
        }
        else
        {
            std::println("{}: FAILED", test);
            ++failed;
        }
    }

    std::println("\n{}/{} passed", passed, passed + failed);
}

// Classic Game Boy green palette
constexpr std::array<U32, 4> Palette = {
    0xFF9BBC0F,  // Lightest (color 0)
    0xFF8BAC0F,  // Light (color 1)
    0xFF306230,  // Dark (color 2)
    0xFF0F380F   // Darkest (color 3)
};

constexpr int Scale = 4;
constexpr int WindowWidth = PPU::ScreenWidth * Scale;
constexpr int WindowHeight = PPU::ScreenHeight * Scale;

int main(int argc, char* argv[])
{
    std::println("GameBoy Emulator v0.1.0");
    std::println("========================\n");

    if (argc > 1 && std::string(argv[1]) == "--test")
    {
        RunTests();
        return 0;
    }

    std::string romPath;
    if (argc > 1)
    {
        romPath = argv[1];
    }
    else
    {
        romPath = std::format("{}/halt_bug.gb", paths::TestRoms);
    }

    auto cart = Cartridge::Load(romPath);
    if (!cart)
    {
        std::println(stderr, "Failed to load ROM: {}", cart.error());
        return 1;
    }
    std::println("Loaded: {}", cart->Header().Title);
    std::println("  Type: {:02X}, ROM: {}KB, RAM: {}KB",
        cart->Header().CartridgeType,
        32 << cart->Header().RomSize,
        cart->Header().RamSize == 0 ? 0 : (2 << (cart->Header().RamSize * 2 - 1)));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        std::println(stderr, "SDL init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_AudioSpec audioSpec{};
    audioSpec.freq = APU::SampleRate;
    audioSpec.format = AUDIO_F32SYS;
    audioSpec.channels = 1;
    audioSpec.samples = 1024;
    audioSpec.callback = nullptr;  // Using SDL_QueueAudio

    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(nullptr, 0, &audioSpec, nullptr, 0);
    if (audioDevice == 0)
    {
        std::println(stderr, "Audio device failed: {}", SDL_GetError());
    }
    else
    {
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    SDL_Window* window = SDL_CreateWindow(
        std::format("GameBoy - {}", cart->Header().Title).c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WindowWidth, WindowHeight,
        SDL_WINDOW_SHOWN
    );
    if (!window)
    {
        std::println(stderr, "Window creation failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer)
    {
        std::println(stderr, "Renderer creation failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        PPU::ScreenWidth, PPU::ScreenHeight
    );
    if (!texture)
    {
        std::println(stderr, "Texture creation failed: {}", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    GameBoy gb{std::move(*cart)};
    std::array<U32, PPU::ScreenWidth * PPU::ScreenHeight> pixels{};
    constexpr U32 FrameTimeMs = 16;  // ~60 FPS

    auto statePath = std::filesystem::path(romPath).replace_extension(".ss0").string();

    bool running = true;
    while (running)
    {
        U32 frameStart = SDL_GetTicks();

        SDL_Event event;
        auto& joypad = gb.GetBus().GetJoypad();
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
            if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_F5:
                    if (gb.SaveState(statePath))
                        std::println("State saved");
                    else
                        std::println("Save state failed");
                    break;
                case SDLK_F8:
                    if (gb.LoadState(statePath))
                        std::println("State loaded");
                    else
                        std::println("Load state failed");
                    break;
                case SDLK_RIGHT:  joypad.Press(Joypad::Right); break;
                case SDLK_LEFT:   joypad.Press(Joypad::Left); break;
                case SDLK_UP:     joypad.Press(Joypad::Up); break;
                case SDLK_DOWN:   joypad.Press(Joypad::Down); break;
                case SDLK_z:      joypad.Press(Joypad::A); break;
                case SDLK_x:      joypad.Press(Joypad::B); break;
                case SDLK_RETURN: joypad.Press(Joypad::Start); break;
                case SDLK_RSHIFT: joypad.Press(Joypad::Select); break;
                }
            }
            if (event.type == SDL_KEYUP)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_RIGHT:  joypad.Release(Joypad::Right); break;
                case SDLK_LEFT:   joypad.Release(Joypad::Left); break;
                case SDLK_UP:     joypad.Release(Joypad::Up); break;
                case SDLK_DOWN:   joypad.Release(Joypad::Down); break;
                case SDLK_z:      joypad.Release(Joypad::A); break;
                case SDLK_x:      joypad.Release(Joypad::B); break;
                case SDLK_RETURN: joypad.Release(Joypad::Start); break;
                case SDLK_RSHIFT: joypad.Release(Joypad::Select); break;
                }
            }
        }

        U32 cycles = 0;
        while (!gb.FrameReady() && cycles < 1000000)
        {
            cycles += gb.Step();
        }

        // Convert framebuffer (2-bit colors) to ARGB
        const auto& framebuffer = gb.GetPPU().GetFramebuffer();
        for (int i = 0; i < PPU::ScreenWidth * PPU::ScreenHeight; i++)
        {
            pixels[i] = Palette[framebuffer[i] & 0x03];
        }

        SDL_UpdateTexture(texture, nullptr, pixels.data(), PPU::ScreenWidth * sizeof(U32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        auto& apu = gb.GetAPU();
        if (audioDevice != 0 && apu.GetSampleCount() > 0)
        {
            SDL_QueueAudio(audioDevice, apu.GetAudioBuffer().data(),
                static_cast<U32>(apu.GetSampleCount() * sizeof(float)));
            apu.ClearBuffer();
        }

        U32 frameTime = SDL_GetTicks() - frameStart;
        if (frameTime < FrameTimeMs)
        {
            SDL_Delay(FrameTimeMs - frameTime);
        }
    }

    gb.SaveRAM();

    if (audioDevice != 0)
        SDL_CloseAudioDevice(audioDevice);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
