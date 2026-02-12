#include <rom_selector.hpp>
#include <font.hpp>
#include <types.hpp>
#include <algorithm>
#include <fstream>
#include <format>

namespace {

// Selector uses 2x Game Boy resolution for readability
constexpr S32 LogicalW = 320;
constexpr S32 LogicalH = 288;

constexpr S32 HeaderY = 12;
constexpr S32 ListY = 36;
constexpr S32 FooterY = LogicalH - 16;
constexpr S32 EntryHeight = 12;  // 8px text + 4px gap
constexpr S32 LeftPad = 12;

// Colors (ARGB) — high contrast GB-inspired
constexpr U32 ColorBg       = 0xFF0F380F;  // Dark green background
constexpr U32 ColorHeader   = 0xFF9BBC0F;  // Bright green for header
constexpr U32 ColorText     = 0xFF8BAC0F;  // Light green for ROM names
constexpr U32 ColorDim      = 0xFF306230;  // Dim green for unselected / info
constexpr U32 ColorSelBg    = 0xFF9BBC0F;  // Bright green selection bar
constexpr U32 ColorSelText  = 0xFF0F380F;  // Dark text on bright bar

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

std::string ReadRomTitle(const std::filesystem::path& path)
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

} // namespace

std::vector<RomEntry> ScanRoms(const std::filesystem::path& dir)
{
    std::vector<RomEntry> roms;

    if (!std::filesystem::is_directory(dir)) return roms;

    for (const auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".gb" && ext != ".gbc") continue;

        auto title = ReadRomTitle(entry.path());
        if (title.empty()) title = entry.path().stem().string();

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
    const std::vector<RomEntry>& roms)
{
    if (roms.empty()) return std::nullopt;

    // Use higher logical resolution for the selector
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

        // Render
        SDL_SetRenderDrawColor(renderer, 0x0F, 0x38, 0x0F, 0xFF);
        SDL_RenderClear(renderer);

        // Header
        DrawText(renderer, LeftPad, HeaderY, "GAMEBOY EMULATOR", ColorHeader);

        // ROM count on the right
        S32 infoX = LogicalW - LeftPad - static_cast<S32>(info.size()) * 6;
        DrawText(renderer, infoX, HeaderY, info.c_str(), ColorDim);

        // Separator line
        SDL_SetRenderDrawColor(renderer, 0x30, 0x62, 0x30, 0xFF);
        SDL_RenderDrawLine(renderer, LeftPad, HeaderY + 12, LogicalW - LeftPad, HeaderY + 12);

        // ROM list
        for (S32 i = 0; i < visibleCount && (scrollOffset + i) < static_cast<S32>(roms.size()); i++)
        {
            S32 idx = scrollOffset + i;
            S32 y = ListY + i * EntryHeight;

            if (idx == selected)
            {
                // Bright selection bar
                SDL_SetRenderDrawColor(renderer, 0x9B, 0xBC, 0x0F, 0xFF);
                SDL_Rect bar = {LeftPad - 2, y - 2, LogicalW - LeftPad * 2 + 4, EntryHeight};
                SDL_RenderFillRect(renderer, &bar);

                // Dark text on bright bar
                DrawText(renderer, LeftPad, y, ">", ColorSelText);
                DrawText(renderer, LeftPad + 10, y, roms[idx].Title.c_str(), ColorSelText, maxChars);
            }
            else
            {
                DrawText(renderer, LeftPad + 10, y, roms[idx].Title.c_str(), ColorText, maxChars);
            }
        }

        // Scroll indicators
        if (scrollOffset > 0)
        {
            SDL_SetRenderDrawColor(renderer, 0x8B, 0xAC, 0x0F, 0xFF);
            S32 cx = LogicalW / 2;
            // Small up arrow
            SDL_RenderDrawLine(renderer, cx, ListY - 6, cx - 4, ListY - 2);
            SDL_RenderDrawLine(renderer, cx, ListY - 6, cx + 4, ListY - 2);
        }
        if (scrollOffset + visibleCount < static_cast<S32>(roms.size()))
        {
            SDL_SetRenderDrawColor(renderer, 0x8B, 0xAC, 0x0F, 0xFF);
            S32 cx = LogicalW / 2;
            S32 by = FooterY - 4;
            SDL_RenderDrawLine(renderer, cx, by + 4, cx - 4, by);
            SDL_RenderDrawLine(renderer, cx, by + 4, cx + 4, by);
        }

        // Footer
        DrawText(renderer, LeftPad, FooterY, roms[selected].Filename.c_str(), ColorDim, maxChars);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
}
