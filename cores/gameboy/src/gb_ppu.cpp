#include <gb_ppu.hpp>
#include <algorithm>
#include <ostream>
#include <istream>
#include <state.hpp>

namespace gb {

PPU::PPU(bool cgbMode)
    : m_CgbMode{cgbMode}
{
}

void PPU::Tick(U8 mCycles)
{
    // When LCD is off, still count cycles for frame timing
    if (!(m_LCDC & 0x80))
    {
        m_Cycles += mCycles;
        // 154 scanlines * 456 cycles = 70224 cycles per frame
        if (m_Cycles >= 70224)
        {
            m_Cycles -= static_cast<U16>(70224);
            m_FrameReady = true;
        }
        return;
    }

    m_Cycles += mCycles;

    switch (m_Mode)
    {
    case PPUMode::OAMScan:
        if (m_Cycles >= OAMScanCycles)
        {
            m_Mode = PPUMode::Drawing;
        }
        break;

    case PPUMode::Drawing:
        if (m_Cycles >= OAMScanCycles + DrawingCycles)
        {
            m_Mode = PPUMode::HBlank;
            m_HBlankStart = true;
            DrawScanline();
            // STAT interrupt on Mode 0 (HBlank) if bit 3 is set
            if (m_STAT & 0x08)
                m_StatInterrupt = true;
        }
        break;

    case PPUMode::HBlank:
        if (m_Cycles >= CyclesPerScanline)
        {
            m_Cycles -= CyclesPerScanline;
            m_LY++;

            if (m_LY == ScreenHeight)
            {
                m_Mode = PPUMode::VBlank;
                m_VBlankInterrupt = true;
                // STAT interrupt on Mode 1 (VBlank) if bit 4 is set
                if (m_STAT & 0x10)
                    m_StatInterrupt = true;
            }
            else
            {
                m_Mode = PPUMode::OAMScan;
                // STAT interrupt on Mode 2 (OAM Scan) if bit 5 is set
                if (m_STAT & 0x20)
                    m_StatInterrupt = true;
            }
        }
        break;

    case PPUMode::VBlank:
        if (m_Cycles >= CyclesPerScanline)
        {
            m_Cycles -= CyclesPerScanline;
            m_LY++;

            if (m_LY > 153)
            {
                m_LY = 0;
                m_WindowLine = 0;
                m_Mode = PPUMode::OAMScan;
                m_FrameReady = true;
                // STAT interrupt on Mode 2 (OAM Scan) if bit 5 is set
                if (m_STAT & 0x20)
                    m_StatInterrupt = true;
            }
        }
        break;
    }

    // Update STAT mode bits (bits 0-1)
    m_STAT = (m_STAT & 0xFC) | static_cast<U8>(m_Mode);

    // LY == LYC check
    if (m_LY == m_LYC)
    {
        m_STAT |= 0x04; // Set coincidence flag (bit 2)
        if (m_STAT & 0x40) // LYC=LY interrupt enabled (bit 6)
        {
            m_StatInterrupt = true;
        }
    }
    else
    {
        m_STAT &= ~0x04;
    }
}

std::optional<U8> PPU::Read(U16 address) const
{
    switch (address)
    {
    case 0xFF40: return m_LCDC;
    case 0xFF41: return m_STAT;
    case 0xFF42: return m_SCY;
    case 0xFF43: return m_SCX;
    case 0xFF44: return m_LY;
    case 0xFF45: return m_LYC;
    case 0xFF47: return m_BGP;
    case 0xFF48: return m_OBP0;
    case 0xFF49: return m_OBP1;
    case 0xFF4A: return m_WY;
    case 0xFF4B: return m_WX;
    case 0xFF4F: return m_CgbMode ? static_cast<U8>(m_VBK | 0xFE) : std::optional<U8>{};
    case 0xFF68: return m_CgbMode ? std::optional<U8>{m_BCPS} : std::nullopt;
    case 0xFF69: return m_CgbMode ? std::optional<U8>{m_BgPaletteRAM[m_BCPS & 0x3F]} : std::nullopt;
    case 0xFF6A: return m_CgbMode ? std::optional<U8>{m_OCPS} : std::nullopt;
    case 0xFF6B: return m_CgbMode ? std::optional<U8>{m_ObjPaletteRAM[m_OCPS & 0x3F]} : std::nullopt;
    default: return std::nullopt;
    }
}

bool PPU::Write(U16 address, U8 value)
{
    switch (address)
    {
    case 0xFF40:
        // LCD being turned off (bit 7: 1->0)
        if ((m_LCDC & 0x80) && !(value & 0x80))
        {
            m_LY = 0;
            m_Cycles = 0;
            m_Mode = PPUMode::HBlank;
            m_STAT = (m_STAT & 0xFC);
        }
        m_LCDC = value;
        return true;
    case 0xFF41:
        // Lower 3 bits are read-only
        m_STAT = (m_STAT & 0x07) | (value & 0xF8);
        return true;
    case 0xFF42: m_SCY = value;
        return true;
    case 0xFF43: m_SCX = value;
        return true;
    case 0xFF44: return true; // LY is read-only
    case 0xFF45: m_LYC = value;
        return true;
    case 0xFF47: m_BGP = value;
        return true;
    case 0xFF48: m_OBP0 = value;
        return true;
    case 0xFF49: m_OBP1 = value;
        return true;
    case 0xFF4A: m_WY = value;
        return true;
    case 0xFF4B: m_WX = value;
        return true;
    case 0xFF4F:
        if (m_CgbMode) m_VBK = value & 0x01;
        return m_CgbMode;
    case 0xFF68:
        if (m_CgbMode) m_BCPS = value;
        return m_CgbMode;
    case 0xFF69:
        if (m_CgbMode) {
            m_BgPaletteRAM[m_BCPS & 0x3F] = value;
            if (m_BCPS & 0x80) m_BCPS = (m_BCPS & 0x80) | ((m_BCPS + 1) & 0x3F);
        }
        return m_CgbMode;
    case 0xFF6A:
        if (m_CgbMode) m_OCPS = value;
        return m_CgbMode;
    case 0xFF6B:
        if (m_CgbMode) {
            m_ObjPaletteRAM[m_OCPS & 0x3F] = value;
            if (m_OCPS & 0x80) m_OCPS = (m_OCPS & 0x80) | ((m_OCPS + 1) & 0x3F);
        }
        return m_CgbMode;
    default: return false;
    }
}

bool PPU::VBlankInterruptRequested()
{
    bool requested = m_VBlankInterrupt;
    m_VBlankInterrupt = false;
    return requested;
}

bool PPU::StatInterruptRequested()
{
    bool requested = m_StatInterrupt;
    m_StatInterrupt = false;
    return requested;
}

bool PPU::FrameReady()
{
    bool ready = m_FrameReady;
    m_FrameReady = false;
    return ready;
}

bool PPU::HBlankStarted()
{
    bool started = m_HBlankStart;
    m_HBlankStart = false;
    return started;
}

U8 PPU::ReadVRAM(U16 address) const
{
    if (m_CgbMode)
        return m_VRAM[(m_VBK & 1) * 0x2000 + (address & 0x1FFF)];
    return m_VRAM[address & 0x1FFF];
}

void PPU::WriteVRAM(U16 address, U8 value)
{
    if (m_CgbMode)
        m_VRAM[(m_VBK & 1) * 0x2000 + (address & 0x1FFF)] = value;
    else
        m_VRAM[address & 0x1FFF] = value;
}

U8 PPU::ReadOAM(U16 address) const
{
    // TODO: Block during Mode 2/3 for accuracy
    return m_OAM[address & 0xFF];
}

void PPU::WriteOAM(U16 address, U8 value)
{
    // TODO: Block during Mode 2/3 for accuracy
    m_OAM[address & 0xFF] = value;
}

void PPU::DrawScanline()
{
    if (!(m_LCDC & 0x80))
        return;

    // Clear per-scanline tracking
    m_BgColorIndices.fill(0);
    m_BgAttributes.fill(0);

    // Background (LCDC bit 0 on DMG disables BG; on CGB it controls priority only)
    const bool bgEnabled = m_LCDC & 0x01;
    if (bgEnabled || m_CgbMode)
    {
        const U16 tileMapBase = (m_LCDC & 0x08) ? 0x1C00 : 0x1800;
        const bool unsignedMode = m_LCDC & 0x10;

        const U8 bgY = (m_SCY + m_LY) & 0xFF;
        const U8 tileY = bgY / 8;
        const U8 pixelY = bgY % 8;

        for (S32 x = 0; x < ScreenWidth; x++)
        {
            const U8 bgX = (m_SCX + x) & 0xFF;
            const U8 tileX = bgX / 8;

            const U16 tileMapAddr = tileMapBase + tileY * 32 + tileX;
            const U8 tileIndex = m_VRAM[tileMapAddr];

            U16 tileDataAddr;
            if (unsignedMode)
                tileDataAddr = tileIndex * 16;
            else
                tileDataAddr = 0x1000 + static_cast<S8>(tileIndex) * 16;

            if (m_CgbMode)
            {
                const U8 attrs = m_VRAM[0x2000 + tileMapAddr];
                const U8 cgbPalette = attrs & 0x07;
                const U16 bankOffset = (attrs & 0x08) ? 0x2000 : 0;
                const bool hFlip = attrs & 0x20;
                const bool vFlip = attrs & 0x40;

                const U8 effectiveY = vFlip ? (7 - pixelY) : pixelY;
                const U16 rowAddr = tileDataAddr + effectiveY * 2;
                const U8 pixelX = bgX % 8;
                const U8 bit = hFlip ? pixelX : (7 - pixelX);
                const U8 low = (m_VRAM[bankOffset + rowAddr] >> bit) & 1;
                const U8 high = (m_VRAM[bankOffset + rowAddr + 1] >> bit) & 1;
                const U8 colorIndex = (high << 1) | low;

                const U8 palOffset = cgbPalette * 8 + colorIndex * 2;
                m_Framebuffer[m_LY * ScreenWidth + x] = CgbColorToARGB(m_BgPaletteRAM[palOffset], m_BgPaletteRAM[palOffset + 1]);
                m_BgColorIndices[x] = colorIndex;
                m_BgAttributes[x] = attrs;
            }
            else
            {
                const U16 rowAddr = tileDataAddr + pixelY * 2;
                const U8 pixelX = bgX % 8;
                const U8 bit = 7 - pixelX;
                const U8 low = (m_VRAM[rowAddr] >> bit) & 1;
                const U8 high = (m_VRAM[rowAddr + 1] >> bit) & 1;
                const U8 colorIndex = (high << 1) | low;

                m_Framebuffer[m_LY * ScreenWidth + x] = DmgPalette[GetColorFromPalette(m_BGP, colorIndex)];
                m_BgColorIndices[x] = colorIndex;
            }
        }
    }

    // Window (LCDC bit 5, WY <= LY)
    if ((m_LCDC & 0x20) && m_WY <= m_LY)
    {
        const S32 windowX = m_WX - 7;

        if (windowX < ScreenWidth)
        {
            const U16 tileMapBase = (m_LCDC & 0x40) ? 0x1C00 : 0x1800;
            const bool unsignedMode = m_LCDC & 0x10;

            const U8 tileY = m_WindowLine / 8;
            const U8 pixelY = m_WindowLine % 8;

            for (S32 x = (windowX < 0 ? 0 : windowX); x < ScreenWidth; x++)
            {
                const U8 winX = static_cast<U8>(x - windowX);
                const U8 tileX = winX / 8;

                const U16 tileMapAddr = tileMapBase + tileY * 32 + tileX;
                const U8 tileIndex = m_VRAM[tileMapAddr];

                U16 tileDataAddr;
                if (unsignedMode)
                    tileDataAddr = tileIndex * 16;
                else
                    tileDataAddr = 0x1000 + static_cast<S8>(tileIndex) * 16;

                if (m_CgbMode)
                {
                    const U8 attrs = m_VRAM[0x2000 + tileMapAddr];
                    const U8 cgbPalette = attrs & 0x07;
                    const U16 bankOffset = (attrs & 0x08) ? 0x2000 : 0;
                    const bool hFlip = attrs & 0x20;
                    const bool vFlip = attrs & 0x40;

                    const U8 effectiveY = vFlip ? (7 - pixelY) : pixelY;
                    const U16 rowAddr = tileDataAddr + effectiveY * 2;
                    const U8 pixelX = winX % 8;
                    const U8 bit = hFlip ? pixelX : (7 - pixelX);
                    const U8 low = (m_VRAM[bankOffset + rowAddr] >> bit) & 1;
                    const U8 high = (m_VRAM[bankOffset + rowAddr + 1] >> bit) & 1;
                    const U8 colorIndex = (high << 1) | low;

                    const U8 palOffset = cgbPalette * 8 + colorIndex * 2;
                    m_Framebuffer[m_LY * ScreenWidth + x] = CgbColorToARGB(m_BgPaletteRAM[palOffset], m_BgPaletteRAM[palOffset + 1]);
                    m_BgColorIndices[x] = colorIndex;
                    m_BgAttributes[x] = attrs;
                }
                else
                {
                    const U16 rowAddr = tileDataAddr + pixelY * 2;
                    const U8 pixelX = winX % 8;
                    const U8 bit = 7 - pixelX;
                    const U8 low = (m_VRAM[rowAddr] >> bit) & 1;
                    const U8 high = (m_VRAM[rowAddr + 1] >> bit) & 1;
                    const U8 colorIndex = (high << 1) | low;

                    m_Framebuffer[m_LY * ScreenWidth + x] = DmgPalette[GetColorFromPalette(m_BGP, colorIndex)];
                    m_BgColorIndices[x] = colorIndex;
                }
            }

            m_WindowLine++;
        }
    }

    // Sprites (LCDC bit 1)
    if (m_LCDC & 0x02)
    {
        const U8 spriteHeight = (m_LCDC & 0x04) ? 16 : 8;

        struct SpriteEntry {
            S32 x;
            S32 y;
            U8 tile;
            U8 attrs;
            U8 oamIndex;
        };
        std::array<SpriteEntry, 10> sprites;
        S32 spriteCount = 0;

        for (S32 i = 0; i < 40 && spriteCount < 10; i++)
        {
            const S32 y = static_cast<S32>(m_OAM[i * 4 + 0]) - 16;
            const S32 x = static_cast<S32>(m_OAM[i * 4 + 1]) - 8;
            const U8 tile = m_OAM[i * 4 + 2];
            const U8 attrs = m_OAM[i * 4 + 3];

            if (static_cast<S32>(m_LY) >= y && static_cast<S32>(m_LY) < y + spriteHeight)
            {
                sprites[spriteCount++] = {x, y, tile, attrs, static_cast<U8>(i)};
            }
        }

        // DMG: sort by X (lower X = higher priority). CGB: OAM order only.
        if (!m_CgbMode)
        {
            for (S32 i = 0; i < spriteCount - 1; i++)
            {
                for (S32 j = i + 1; j < spriteCount; j++)
                {
                    if (sprites[j].x < sprites[i].x)
                        std::swap(sprites[i], sprites[j]);
                }
            }
        }

        // Draw in reverse order (lowest priority first)
        for (S32 i = spriteCount - 1; i >= 0; i--)
        {
            const auto& sprite = sprites[i];
            const bool xFlip = sprite.attrs & 0x20;
            const bool yFlip = sprite.attrs & 0x40;
            const bool oamBgPriority = sprite.attrs & 0x80;

            U8 row = static_cast<U8>(m_LY - sprite.y);
            if (yFlip)
                row = spriteHeight - 1 - row;

            U8 tileIndex = sprite.tile;
            if (spriteHeight == 16)
                tileIndex &= 0xFE;

            const U16 tileDataAddr = tileIndex * 16 + row * 2;
            const U16 bankOffset = (m_CgbMode && (sprite.attrs & 0x08)) ? 0x2000 : 0;

            for (S32 px = 0; px < 8; px++)
            {
                const S32 screenX = sprite.x + px;
                if (screenX < 0 || screenX >= ScreenWidth)
                    continue;

                const U8 bit = static_cast<U8>(xFlip ? px : (7 - px));
                const U8 low = (m_VRAM[bankOffset + tileDataAddr] >> bit) & 1;
                const U8 high = (m_VRAM[bankOffset + tileDataAddr + 1] >> bit) & 1;
                const U8 colorIndex = (high << 1) | low;

                if (colorIndex == 0)
                    continue;

                // Priority check
                if (m_CgbMode)
                {
                    // CGB: sprite hidden behind BG if (LCDC bit 0 enabled) AND (bgColorIndex != 0)
                    // AND (OAM priority bit OR BG attr priority bit)
                    if (bgEnabled && m_BgColorIndices[screenX] != 0)
                    {
                        if (oamBgPriority || (m_BgAttributes[screenX] & 0x80))
                            continue;
                    }

                    const U8 cgbPalette = sprite.attrs & 0x07;
                    const U8 palOffset = cgbPalette * 8 + colorIndex * 2;
                    m_Framebuffer[m_LY * ScreenWidth + screenX] = CgbColorToARGB(m_ObjPaletteRAM[palOffset], m_ObjPaletteRAM[palOffset + 1]);
                }
                else
                {
                    if (oamBgPriority && m_BgColorIndices[screenX] != 0)
                        continue;

                    const U8 palette = (sprite.attrs & 0x10) ? m_OBP1 : m_OBP0;
                    m_Framebuffer[m_LY * ScreenWidth + screenX] = DmgPalette[GetColorFromPalette(palette, colorIndex)];
                }
            }
        }
    }
}

U8 PPU::GetColorFromPalette(U8 palette, U8 colorIndex)
{
    return (palette >> (colorIndex * 2)) & 0x03;
}

U32 PPU::CgbColorToARGB(U8 low, U8 high)
{
    U16 color = low | (static_cast<U16>(high) << 8);
    U8 r = static_cast<U8>((color & 0x1F) * 255 / 31);
    U8 g = static_cast<U8>(((color >> 5) & 0x1F) * 255 / 31);
    U8 b = static_cast<U8>(((color >> 10) & 0x1F) * 255 / 31);
    return 0xFF000000 | (static_cast<U32>(r) << 16) | (static_cast<U32>(g) << 8) | b;
}

void PPU::SaveState(std::ostream& out) const
{
    state::Write(out, m_Cycles);
    state::Write(out, static_cast<U8>(m_Mode));
    state::Write(out, m_LCDC);
    state::Write(out, m_STAT);
    state::Write(out, m_SCY);
    state::Write(out, m_SCX);
    state::Write(out, m_LY);
    state::Write(out, m_LYC);
    state::Write(out, m_BGP);
    state::Write(out, m_OBP0);
    state::Write(out, m_OBP1);
    state::Write(out, m_WY);
    state::Write(out, m_WX);
    state::Write(out, m_VRAM);
    state::Write(out, m_OAM);
    state::Write(out, m_Framebuffer);
    state::Write(out, m_WindowLine);
    state::Write(out, m_VBlankInterrupt);
    state::Write(out, m_StatInterrupt);
    state::Write(out, m_FrameReady);
    // CGB fields
    state::Write(out, m_VBK);
    state::Write(out, m_BCPS);
    state::Write(out, m_OCPS);
    state::Write(out, m_BgPaletteRAM);
    state::Write(out, m_ObjPaletteRAM);
}

void PPU::LoadState(std::istream& in)
{
    state::Read(in, m_Cycles);
    U8 mode;
    state::Read(in, mode);
    m_Mode = static_cast<PPUMode>(mode);
    state::Read(in, m_LCDC);
    state::Read(in, m_STAT);
    state::Read(in, m_SCY);
    state::Read(in, m_SCX);
    state::Read(in, m_LY);
    state::Read(in, m_LYC);
    state::Read(in, m_BGP);
    state::Read(in, m_OBP0);
    state::Read(in, m_OBP1);
    state::Read(in, m_WY);
    state::Read(in, m_WX);
    state::Read(in, m_VRAM);
    state::Read(in, m_OAM);
    state::Read(in, m_Framebuffer);
    state::Read(in, m_WindowLine);
    state::Read(in, m_VBlankInterrupt);
    state::Read(in, m_StatInterrupt);
    state::Read(in, m_FrameReady);
    // CGB fields
    state::Read(in, m_VBK);
    state::Read(in, m_BCPS);
    state::Read(in, m_OCPS);
    state::Read(in, m_BgPaletteRAM);
    state::Read(in, m_ObjPaletteRAM);
}

} // namespace gb
