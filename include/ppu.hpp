#pragma once

#include <array>
#include <iosfwd>
#include <optional>
#include <types.hpp>

enum class PPUMode : U8 {
    HBlank = 0,   // Mode 0: Horizontal blank (204 cycles)
    VBlank = 1,   // Mode 1: Vertical blank (4560 cycles total, 10 lines)
    OAMScan = 2,  // Mode 2: Scanning OAM for sprites (80 cycles)
    Drawing = 3   // Mode 3: Drawing pixels (172 cycles, variable)
};

class PPU {
public:
    static constexpr int ScreenWidth = 160;
    static constexpr int ScreenHeight = 144;
    static constexpr int CyclesPerScanline = 456;
    static constexpr int OAMScanCycles = 80;
    static constexpr int DrawingCycles = 172;
    static constexpr int HBlankCycles = 204;
    static constexpr int VBlankLines = 10;

    PPU();

    void Tick(U8 mCycles);

    [[nodiscard]] std::optional<U8> Read(U16 address) const;
    bool Write(U16 address, U8 value);

    // Clears the flag after reading
    [[nodiscard]] bool VBlankInterruptRequested();
    [[nodiscard]] bool StatInterruptRequested();
    [[nodiscard]] bool FrameReady();

    [[nodiscard]] const std::array<U8, ScreenWidth * ScreenHeight>& GetFramebuffer() const { return m_Framebuffer; }

    [[nodiscard]] U8 GetLY() const { return m_LY; }
    [[nodiscard]] U8 GetLCDC() const { return m_LCDC; }

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

    std::array<U8, 0x2000> m_VRAM{};  // 8KB Video RAM
    std::array<U8, 0xA0> m_OAM{};     // 160 bytes OAM

    std::array<U8, ScreenWidth * ScreenHeight> m_Framebuffer{};

    // Only increments when window is visible on current scanline
    U8 m_WindowLine{};

    bool m_VBlankInterrupt{};
    bool m_StatInterrupt{};
    bool m_FrameReady{};

    void DrawScanline();
    [[nodiscard]] static U8 GetColorFromPalette(U8 palette, U8 colorIndex);
};
