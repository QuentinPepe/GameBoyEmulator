#include <gb_run.hpp>
#include <SDL.h>
#include <print>
#include <format>
#include <filesystem>
#include <vector>

#include <gb.hpp>
#include <gb_ppu.hpp>
#include <gb_apu.hpp>
#include <gb_joypad.hpp>

namespace gb {

void RunTests(const std::string& testRomsDir)
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

    S32 passed = 0, failed = 0;

    for (const auto& test : tests)
    {
        auto romPath = (std::filesystem::path(testRomsDir) / test).string();
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

constexpr S32 Scale = 4;
constexpr S32 WindowWidth = PPU::ScreenWidth * Scale;
constexpr S32 WindowHeight = PPU::ScreenHeight * Scale;

S32 Run(const std::string& romPath, bool fullscreen)
{
    auto cart = Cartridge::Load(romPath);
    if (!cart)
    {
        std::println(stderr, "Failed to load ROM: {}", cart.error());
        return 1;
    }
    std::println("Loaded: {}", cart->Header().Title);
    std::println("  Mode: {}", cart->IsCgbMode() ? "Game Boy Color" : "DMG");
    std::println("  Type: {:02X}, ROM: {}KB, RAM: {}KB",
        cart->Header().CartridgeType,
        32 << cart->Header().RomSize,
        cart->Header().RamSize == 0 ? 0 : (2 << (cart->Header().RamSize * 2 - 1)));

    // Set up save directory
    auto romStem = std::filesystem::path(romPath).stem().string();
    std::string statePath;
    char* prefPath = SDL_GetPrefPath("", "Phosphor");
    if (prefPath)
    {
        auto saveDir = std::filesystem::path(prefPath) / "GameBoy";
        std::filesystem::create_directories(saveDir);
        SDL_free(prefPath);
        cart->SetSavePath(saveDir / (romStem + ".sav"));
        statePath = (saveDir / (romStem + ".ss0")).string();
    }
    else
    {
        statePath = std::filesystem::path(romPath).replace_extension(".ss0").string();
    }

    // Audio
    SDL_AudioSpec audioSpec{};
    audioSpec.freq = APU::SampleRate;
    audioSpec.format = AUDIO_F32SYS;
    audioSpec.channels = 1;
    audioSpec.samples = 1024;
    audioSpec.callback = nullptr;

    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(nullptr, 0, &audioSpec, nullptr, 0);
    if (audioDevice == 0)
        std::println(stderr, "Audio device failed: {}", SDL_GetError());
    else
        SDL_PauseAudioDevice(audioDevice, 0);

    // Game window
    SDL_Window* window = SDL_CreateWindow(
        std::format("{} - {}", cart->IsCgbMode() ? "GameBoy Color" : "GameBoy", cart->Header().Title).c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WindowWidth, WindowHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    if (!window)
    {
        std::println(stderr, "Window creation failed: {}", SDL_GetError());
        return 1;
    }

    if (fullscreen)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::println(stderr, "Renderer creation failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
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
        return 1;
    }

    SDL_RenderSetLogicalSize(renderer, PPU::ScreenWidth, PPU::ScreenHeight);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

    GameBoy gb{std::move(*cart)};

    // Open first available game controller
    SDL_GameController* controller = nullptr;
    for (S32 i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            controller = SDL_GameControllerOpen(i);
            if (controller)
            {
                std::println("Controller: {}", SDL_GameControllerName(controller));
                break;
            }
        }
    }

    bool running = true;
    while (running)
    {
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
                case SDLK_F11:
                {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, flags & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    break;
                }
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
            if (event.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: joypad.Press(Joypad::Right); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  joypad.Press(Joypad::Left); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    joypad.Press(Joypad::Up); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  joypad.Press(Joypad::Down); break;
                case SDL_CONTROLLER_BUTTON_A:          joypad.Press(Joypad::A); break;
                case SDL_CONTROLLER_BUTTON_B:          joypad.Press(Joypad::B); break;
                case SDL_CONTROLLER_BUTTON_START:      joypad.Press(Joypad::Start); break;
                case SDL_CONTROLLER_BUTTON_BACK:       joypad.Press(Joypad::Select); break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    if (gb.SaveState(statePath))
                        std::println("State saved");
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    if (gb.LoadState(statePath))
                        std::println("State loaded");
                    break;
                case SDL_CONTROLLER_BUTTON_GUIDE:
                {
                    Uint32 flags = SDL_GetWindowFlags(window);
                    SDL_SetWindowFullscreen(window, flags & SDL_WINDOW_FULLSCREEN_DESKTOP ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    break;
                }
                }
            }
            if (event.type == SDL_CONTROLLERBUTTONUP)
            {
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: joypad.Release(Joypad::Right); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  joypad.Release(Joypad::Left); break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    joypad.Release(Joypad::Up); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  joypad.Release(Joypad::Down); break;
                case SDL_CONTROLLER_BUTTON_A:          joypad.Release(Joypad::A); break;
                case SDL_CONTROLLER_BUTTON_B:          joypad.Release(Joypad::B); break;
                case SDL_CONTROLLER_BUTTON_START:      joypad.Release(Joypad::Start); break;
                case SDL_CONTROLLER_BUTTON_BACK:       joypad.Release(Joypad::Select); break;
                }
            }
            if (event.type == SDL_CONTROLLERDEVICEADDED && !controller)
            {
                controller = SDL_GameControllerOpen(event.cdevice.which);
                if (controller)
                    std::println("Controller connected: {}", SDL_GameControllerName(controller));
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && controller)
            {
                if (event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)))
                {
                    std::println("Controller disconnected");
                    SDL_GameControllerClose(controller);
                    controller = nullptr;
                }
            }
        }

        U32 cycles = 0;
        while (!gb.FrameReady() && cycles < 1000000)
        {
            cycles += gb.Step();
        }

        SDL_UpdateTexture(texture, nullptr, gb.GetPPU().GetFramebuffer().data(), PPU::ScreenWidth * sizeof(U32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        auto& apu = gb.GetAPU();
        if (audioDevice != 0 && apu.GetSampleCount() > 0)
        {
            U32 queued = SDL_GetQueuedAudioSize(audioDevice);
            constexpr U32 MaxQueueBytes = APU::SampleRate * sizeof(float) / 15;
            if (queued < MaxQueueBytes)
            {
                SDL_QueueAudio(audioDevice, apu.GetAudioBuffer().data(),
                    static_cast<U32>(apu.GetSampleCount() * sizeof(float)));
            }
            apu.ClearBuffer();
        }
    }

    gb.SaveRAM();

    if (controller)
        SDL_GameControllerClose(controller);
    if (audioDevice != 0)
        SDL_CloseAudioDevice(audioDevice);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return 0;
}

} // namespace gb
