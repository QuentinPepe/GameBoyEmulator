#include <rom_selector.hpp>
#include <font.hpp>
#include <types.hpp>
#include <algorithm>
#include <fstream>
#include <format>
#include <cstring>

namespace {

constexpr S32 LogicalW = 320;
constexpr S32 LogicalH = 288;

constexpr S32 HeaderY = 12;
constexpr S32 ListY = 36;
constexpr S32 FooterY = LogicalH - 16;
constexpr S32 EntryHeight = 12;
constexpr S32 LeftPad = 12;

constexpr U32 ColorBg       = 0xFF0A0A0F;
constexpr U32 ColorHeader   = 0xFFDA70D6;
constexpr U32 ColorText     = 0xFFB8A9C9;
constexpr U32 ColorDim      = 0xFF4A3A5C;
constexpr U32 ColorSelBg    = 0xFFFF69B4;
constexpr U32 ColorSelText  = 0xFF0A0A0F;

void SetBgColor(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0x0A, 0x0A, 0x0F, 0xFF);
}

void SetSepColor(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0x4A, 0x3A, 0x5C, 0xFF);
}

void DrawChar(SDL_Renderer* renderer, S32 x, S32 y, char ch, U32 color)
{
    if (ch < font::FirstChar || ch >= font::LastChar) return;
    const auto& glyph = font::Glyphs[ch - font::FirstChar];

    SDL_SetRenderDrawColor(renderer,
        (color >> 16) & 0xFF,
        (color >> 8) & 0xFF,
        color & 0xFF,
        (color >> 24) & 0xFF);

    for (S32 row = 0; row < 8; row++)
    {
        U8 bits = glyph[row];
        for (S32 col = 0; col < 8; col++)
        {
            if (bits & (0x80 >> col))
            {
                SDL_RenderDrawPoint(renderer, x + col, y + row);
            }
        }
    }
}

void DrawText(SDL_Renderer* renderer, S32 x, S32 y, const char* text, U32 color, S32 maxChars = 0)
{
    S32 count = 0;
    for (const char* p = text; *p; p++, count++)
    {
        if (maxChars > 0 && count >= maxChars) break;
        DrawChar(renderer, x, y, *p, color);
        x += 6;
    }
}

std::string ReadGBTitle(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file) return {};

    file.seekg(0x0134);
    char buf[16]{};
    file.read(buf, 16);

    std::string title;
    for (S32 i = 0; i < 16 && buf[i] != '\0'; i++)
    {
        char c = buf[i];
        if (c >= 32 && c < 127)
            title += c;
    }
    return title;
}

bool HasExtension(const std::string& ext, EmuSystem system)
{
    switch (system)
    {
    case EmuSystem::GameBoy: return ext == ".gb" || ext == ".gbc";
    case EmuSystem::GameBoyAdvance: return ext == ".gba";
    case EmuSystem::PlayStation1:   return ext == ".bin" || ext == ".cue" || ext == ".iso";
    }
    return false;
}

struct SystemInfo {
    const char* name;
    const char* detail;
    bool available;
};

constexpr SystemInfo Systems[] = {
    { "Game Boy",         ".gb .gbc",    true  },
    { "Game Boy Advance", ".gba",        true  },
    { "PlayStation",      "coming soon", false },
};
constexpr S32 SystemCount = sizeof(Systems) / sizeof(Systems[0]);

} // namespace

std::optional<EmuSystem> SelectSystem(SDL_Renderer* renderer)
{
    SDL_RenderSetLogicalSize(renderer, LogicalW, LogicalH);

    S32 selected = 0;
    constexpr S32 SystemEntryHeight = 20;

    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) return std::nullopt;

            bool up = false, down = false, confirm = false, cancel = false;

            if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_UP:     up = true; break;
                case SDLK_DOWN:   down = true; break;
                case SDLK_RETURN: confirm = true; break;
                case SDLK_z:      confirm = true; break;
                case SDLK_ESCAPE: cancel = true; break;
                default: break;
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    up = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  down = true; break;
                case SDL_CONTROLLER_BUTTON_A:          confirm = true; break;
                case SDL_CONTROLLER_BUTTON_B:          cancel = true; break;
                default: break;
                }
            }

            if (cancel) return std::nullopt;
            if (confirm && Systems[selected].available)
                return static_cast<EmuSystem>(selected);

            if (up && selected > 0) selected--;
            if (down && selected < SystemCount - 1) selected++;
        }

        SetBgColor(renderer);
        SDL_RenderClear(renderer);

        DrawText(renderer, LeftPad, HeaderY, "PHOSPHOR", ColorHeader);

        SetSepColor(renderer);
        SDL_RenderDrawLine(renderer, LeftPad, HeaderY + 12, LogicalW - LeftPad, HeaderY + 12);

        for (S32 i = 0; i < SystemCount; i++)
        {
            S32 y = ListY + i * SystemEntryHeight;
            bool isSelected = (i == selected);
            bool isAvailable = Systems[i].available;

            if (isSelected && isAvailable)
            {
                SDL_SetRenderDrawColor(renderer, 0xFF, 0x69, 0xB4, 0xFF);
                SDL_Rect bar = {LeftPad - 2, y - 2, LogicalW - LeftPad * 2 + 4, EntryHeight};
                SDL_RenderFillRect(renderer, &bar);

                DrawText(renderer, LeftPad, y, ">", ColorSelText);
                DrawText(renderer, LeftPad + 10, y, Systems[i].name, ColorSelText);

                S32 detailX = LogicalW - LeftPad - static_cast<S32>(std::strlen(Systems[i].detail)) * 6;
                DrawText(renderer, detailX, y, Systems[i].detail, ColorSelText);
            }
            else
            {
                U32 nameColor = isAvailable ? ColorText : ColorDim;
                if (isSelected) DrawText(renderer, LeftPad, y, ">", ColorDim);
                DrawText(renderer, LeftPad + 10, y, Systems[i].name, nameColor);

                S32 detailX = LogicalW - LeftPad - static_cast<S32>(std::strlen(Systems[i].detail)) * 6;
                DrawText(renderer, detailX, y, Systems[i].detail, ColorDim);
            }
        }

        DrawText(renderer, LeftPad, FooterY, "Select a system", ColorDim);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
}

std::vector<RomEntry> ScanRoms(const std::filesystem::path& dir, EmuSystem system)
{
    std::vector<RomEntry> roms;

    if (!std::filesystem::is_directory(dir)) return roms;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (!HasExtension(ext, system)) continue;

        std::string title;
        if (system == EmuSystem::GameBoy)
            title = ReadGBTitle(entry.path());
        if (title.empty())
            title = entry.path().stem().string();

        roms.push_back({
            entry.path(),
            std::move(title),
            entry.path().filename().string()
        });
    }

    std::sort(roms.begin(), roms.end(), [](const RomEntry& a, const RomEntry& b) {
        return a.Filename < b.Filename;
    });

    return roms;
}

std::optional<std::filesystem::path> SelectRom(
    SDL_Renderer* renderer,
    const std::vector<RomEntry>& roms,
    const char* header)
{
    if (roms.empty()) return std::nullopt;

    SDL_RenderSetLogicalSize(renderer, LogicalW, LogicalH);

    S32 selected = 0;
    S32 scrollOffset = 0;
    const S32 visibleCount = (FooterY - ListY) / EntryHeight;
    const S32 maxChars = (LogicalW - LeftPad * 2 - 12) / 6;

    auto info = std::format("{} ROM{}", roms.size(), roms.size() > 1 ? "s" : "");

    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) return std::nullopt;

            bool up = false, down = false, confirm = false, cancel = false;

            if (event.type == SDL_KEYDOWN)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_UP:     up = true; break;
                case SDLK_DOWN:   down = true; break;
                case SDLK_RETURN: confirm = true; break;
                case SDLK_z:      confirm = true; break;
                case SDLK_ESCAPE: cancel = true; break;
                default: break;
                }
            }

            if (event.type == SDL_CONTROLLERBUTTONDOWN)
            {
                switch (event.cbutton.button)
                {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    up = true; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  down = true; break;
                case SDL_CONTROLLER_BUTTON_A:          confirm = true; break;
                case SDL_CONTROLLER_BUTTON_B:          cancel = true; break;
                default: break;
                }
            }

            if (cancel) return std::nullopt;
            if (confirm) return roms[selected].Path;

            if (up && selected > 0)
            {
                selected--;
                if (selected < scrollOffset) scrollOffset = selected;
            }
            if (down && selected < static_cast<S32>(roms.size()) - 1)
            {
                selected++;
                if (selected >= scrollOffset + visibleCount)
                    scrollOffset = selected - visibleCount + 1;
            }
        }

        SetBgColor(renderer);
        SDL_RenderClear(renderer);

        DrawText(renderer, LeftPad, HeaderY, header, ColorHeader);

        S32 infoX = LogicalW - LeftPad - static_cast<S32>(info.size()) * 6;
        DrawText(renderer, infoX, HeaderY, info.c_str(), ColorDim);

        SetSepColor(renderer);
        SDL_RenderDrawLine(renderer, LeftPad, HeaderY + 12, LogicalW - LeftPad, HeaderY + 12);

        for (S32 i = 0; i < visibleCount && (scrollOffset + i) < static_cast<S32>(roms.size()); i++)
        {
            S32 idx = scrollOffset + i;
            S32 y = ListY + i * EntryHeight;

            if (idx == selected)
            {
                SDL_SetRenderDrawColor(renderer, 0xFF, 0x69, 0xB4, 0xFF);
                SDL_Rect bar = {LeftPad - 2, y - 2, LogicalW - LeftPad * 2 + 4, EntryHeight};
                SDL_RenderFillRect(renderer, &bar);

                DrawText(renderer, LeftPad, y, ">", ColorSelText);
                DrawText(renderer, LeftPad + 10, y, roms[idx].Title.c_str(), ColorSelText, maxChars);
            }
            else
            {
                DrawText(renderer, LeftPad + 10, y, roms[idx].Title.c_str(), ColorText, maxChars);
            }
        }

        if (scrollOffset > 0)
        {
            SDL_SetRenderDrawColor(renderer, 0xB8, 0xA9, 0xC9, 0xFF);
            S32 cx = LogicalW / 2;
            SDL_RenderDrawLine(renderer, cx, ListY - 6, cx - 4, ListY - 2);
            SDL_RenderDrawLine(renderer, cx, ListY - 6, cx + 4, ListY - 2);
        }
        if (scrollOffset + visibleCount < static_cast<S32>(roms.size()))
        {
            SDL_SetRenderDrawColor(renderer, 0xB8, 0xA9, 0xC9, 0xFF);
            S32 cx = LogicalW / 2;
            S32 by = FooterY - 4;
            SDL_RenderDrawLine(renderer, cx, by + 4, cx - 4, by);
            SDL_RenderDrawLine(renderer, cx, by + 4, cx + 4, by);
        }

        DrawText(renderer, LeftPad, FooterY, roms[selected].Filename.c_str(), ColorDim, maxChars);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
}

void ShowEmptyRomList(SDL_Renderer* renderer, const char* header, const char* romDir)
{
    SDL_RenderSetLogicalSize(renderer, LogicalW, LogicalH);

    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT) return;
            if (event.type == SDL_KEYDOWN) return;
            if (event.type == SDL_CONTROLLERBUTTONDOWN) return;
        }

        SetBgColor(renderer);
        SDL_RenderClear(renderer);

        DrawText(renderer, LeftPad, HeaderY, header, ColorHeader);
        DrawText(renderer, LogicalW - LeftPad - 6 * 6, HeaderY, "0 ROMs", ColorDim);

        SetSepColor(renderer);
        SDL_RenderDrawLine(renderer, LeftPad, HeaderY + 12, LogicalW - LeftPad, HeaderY + 12);

        DrawText(renderer, LeftPad, ListY, "No ROMs found", ColorDim);
        DrawText(renderer, LeftPad, ListY + EntryHeight, romDir, ColorDim);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
}
