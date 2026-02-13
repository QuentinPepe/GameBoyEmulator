#pragma once

#include <array>
#include <iosfwd>
#include <optional>
#include <types.hpp>

namespace gb {

enum class PPUMode : U8 {
    HBlank = 0,   // Mode 0: Horizontal blank (204 cycles)
    VBlank = 1,   // Mode 1: Vertical blank (4560 cycles total, 10 lines)
    OAMScan = 2,  // Mode 2: Scanning OAM for sprites (80 cycles)
    Drawing = 3   // Mode 3: Drawing pixels (172 cycles, variable)
};

class PPU {
public:
    static constexpr S32 ScreenWidth = 160;
    static constexpr S32 ScreenHeight = 144;
    static constexpr S32 CyclesPerScanline = 456;
    static constexpr S32 OAMScanCycles = 80;
    static constexpr S32 DrawingCycles = 172;
    static constexpr S32 HBlankCycles = 204;
    static constexpr S32 VBlankLines = 10;

    explicit PPU(bool cgbMode = false);

    void Tick(U8 mCycles);

    [[nodiscard]] std::optional<U8> Read(U16 address) const;
    bool Write(U16 address, U8 value);

    // Clears the flag after reading
    [[nodiscard]] bool VBlankInterruptRequested();
    [[nodiscard]] bool StatInterruptRequested();
    [[nodiscard]] bool FrameReady();
    [[nodiscard]] bool HBlankStarted();

    [[nodiscard]] const std::array<U32, ScreenWidth * ScreenHeight>& GetFramebuffer() const { return m_Framebuffer; }

    [[nodiscard]] U8 GetLY() const { return m_LY; }
    [[nodiscard]] U8 GetLCDC() const { return m_LCDC; }
    [[nodiscard]] U8 GetVBK() const { return m_VBK; }

    [[nodiscard]] U8 ReadVRAM(U16 address) const;
    void WriteVRAM(U16 address, U8 value);
    [[nodiscard]] U8 ReadOAM(U16 address) const;
    void WriteOAM(U16 address, U8 value);

    void SaveState(std::ostream& out) const;
    void LoadState(std::istream& in);

private:
    U16 m_Cycles{};  // 0-455, position within current scanline

    PPUMode m_Mode{PPUMode::OAMScan};

    U8 m_LCDC{0x91};  // 0xFF40 - LCD Control (default: LCD on, BG on)
    U8 m_STAT{};      // 0xFF41 - LCD Status
    U8 m_SCY{};       // 0xFF42 - Scroll Y
    U8 m_SCX{};       // 0xFF43 - Scroll X
    U8 m_LY{};        // 0xFF44 - Current scanline (0-153)
    U8 m_LYC{};       // 0xFF45 - LY Compare
    U8 m_BGP{0xFC};   // 0xFF47 - BG Palette (default: 11 10 01 00)
    U8 m_OBP0{};      // 0xFF48 - Object Palette 0
    U8 m_OBP1{};      // 0xFF49 - Object Palette 1
    U8 m_WY{};        // 0xFF4A - Window Y
    U8 m_WX{};        // 0xFF4B - Window X

    std::array<U8, 0x4000> m_VRAM{};  // 16KB Video RAM (2 banks in CGB)
    std::array<U8, 0xA0> m_OAM{};     // 160 bytes OAM

    // CGB registers and palette RAM
    U8 m_VBK{0};   // 0xFF4F: VRAM bank select (bit 0)
    U8 m_BCPS{0};  // 0xFF68: BG palette index (bit 7=auto-inc, bits 0-5=index)
    U8 m_OCPS{0};  // 0xFF6A: OBJ palette index
    std::array<U8, 64> m_BgPaletteRAM{};   // 8 palettes x 4 colors x 2 bytes
    std::array<U8, 64> m_ObjPaletteRAM{};  // 8 palettes x 4 colors x 2 bytes

    std::array<U32, ScreenWidth * ScreenHeight> m_Framebuffer{};

    // Per-scanline tracking for sprite priority
    std::array<U8, ScreenWidth> m_BgColorIndices{};  // Raw BG color index (0-3)
    std::array<U8, ScreenWidth> m_BgAttributes{};    // CGB BG tile attributes

    static constexpr std::array<U32, 4> DmgPalette = {
        0xFF9BBC0F,  // Lightest (color 0)
        0xFF8BAC0F,  // Light (color 1)
        0xFF306230,  // Dark (color 2)
        0xFF0F380F   // Darkest (color 3)
    };

    // Only increments when window is visible on current scanline
    U8 m_WindowLine{};

    bool m_VBlankInterrupt{};
    bool m_StatInterrupt{};
    bool m_FrameReady{};
    bool m_HBlankStart{};

    bool m_CgbMode{false};

    void DrawScanline();
    [[nodiscard]] static U8 GetColorFromPalette(U8 palette, U8 colorIndex);
    [[nodiscard]] static U32 CgbColorToARGB(U8 low, U8 high);
};

} // namespace gb
