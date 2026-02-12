#include <ppu.hpp>
#include <algorithm>
#include <ostream>
#include <istream>
#include <state.hpp>

PPU::PPU()
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

U8 PPU::ReadVRAM(U16 address) const
{
    // TODO: Block during Mode 3 for accuracy
    return m_VRAM[address & 0x1FFF];
}

void PPU::WriteVRAM(U16 address, U8 value)
{
    // TODO: Block during Mode 3 for accuracy
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

    // Background (LCDC bit 0)
    if (m_LCDC & 0x01)
    {
        // Tile map base: 0x9800 (bit 3 = 0) or 0x9C00 (bit 3 = 1)
        const U16 tileMapBase = (m_LCDC & 0x08) ? 0x1C00 : 0x1800;

        // Tile data mode: 0x8000 (bit 4 = 1) or 0x8800 (bit 4 = 0)
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
            {
                tileDataAddr = tileIndex * 16;
            }
            else
            {
                tileDataAddr = 0x1000 + static_cast<S8>(tileIndex) * 16;
            }

            // 2 bytes per row
            const U16 rowAddr = tileDataAddr + pixelY * 2;

            const U8 pixelX = bgX % 8;
            const U8 bit = 7 - pixelX;
            const U8 low = (m_VRAM[rowAddr] >> bit) & 1;
            const U8 high = (m_VRAM[rowAddr + 1] >> bit) & 1;
            const U8 colorIndex = (high << 1) | low;

            m_Framebuffer[m_LY * ScreenWidth + x] = GetColorFromPalette(m_BGP, colorIndex);
        }
    }

    // Window (LCDC bit 5, WY <= LY)
    if ((m_LCDC & 0x20) && m_WY <= m_LY)
    {
        // WX=7 means window starts at X=0
        const S32 windowX = m_WX - 7;

        if (windowX < ScreenWidth)
        {
            // Tile map base: 0x9800 (bit 6 = 0) or 0x9C00 (bit 6 = 1)
            const U16 tileMapBase = (m_LCDC & 0x40) ? 0x1C00 : 0x1800;

            // Same tile data mode as background (LCDC bit 4)
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
                {
                    tileDataAddr = tileIndex * 16;
                }
                else
                {
                    tileDataAddr = 0x1000 + static_cast<S8>(tileIndex) * 16;
                }

                // 2 bytes per row
                const U16 rowAddr = tileDataAddr + pixelY * 2;

                const U8 pixelX = winX % 8;
                const U8 bit = 7 - pixelX;
                const U8 low = (m_VRAM[rowAddr] >> bit) & 1;
                const U8 high = (m_VRAM[rowAddr + 1] >> bit) & 1;
                const U8 colorIndex = (high << 1) | low;

                m_Framebuffer[m_LY * ScreenWidth + x] = GetColorFromPalette(m_BGP, colorIndex);
            }

            m_WindowLine++;
        }
    }

    // Sprites (LCDC bit 1)
    if (m_LCDC & 0x02)
    {
        // Sprite height: 8 or 16 pixels (LCDC bit 2)
        const U8 spriteHeight = (m_LCDC & 0x04) ? 16 : 8;

        // Collect visible sprites on this scanline (max 10)
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

        // Sort by X (lower X = higher priority = drawn last)
        // Stable sort to preserve OAM order for same X
        for (S32 i = 0; i < spriteCount - 1; i++)
        {
            for (S32 j = i + 1; j < spriteCount; j++)
            {
                if (sprites[j].x < sprites[i].x)
                {
                    std::swap(sprites[i], sprites[j]);
                }
            }
        }

        // Draw in reverse order (lowest priority first)
        for (S32 i = spriteCount - 1; i >= 0; i--)
        {
            const auto& sprite = sprites[i];
            const U8 palette = (sprite.attrs & 0x10) ? m_OBP1 : m_OBP0;
            const bool xFlip = sprite.attrs & 0x20;
            const bool yFlip = sprite.attrs & 0x40;
            const bool bgPriority = sprite.attrs & 0x80;

            U8 row = static_cast<U8>(m_LY - sprite.y);
            if (yFlip)
                row = spriteHeight - 1 - row;

            // In 8x16 mode, bit 0 of tile index is ignored
            U8 tileIndex = sprite.tile;
            if (spriteHeight == 16)
                tileIndex &= 0xFE;

            // Sprite tile data is always at 0x8000
            const U16 tileDataAddr = tileIndex * 16 + row * 2;

            for (S32 px = 0; px < 8; px++)
            {
                const S32 screenX = sprite.x + px;
                if (screenX < 0 || screenX >= ScreenWidth)
                    continue;

                const U8 bit = static_cast<U8>(xFlip ? px : (7 - px));
                const U8 low = (m_VRAM[tileDataAddr] >> bit) & 1;
                const U8 high = (m_VRAM[tileDataAddr + 1] >> bit) & 1;
                const U8 colorIndex = (high << 1) | low;

                // Color 0 is transparent
                if (colorIndex == 0)
                    continue;

                // BG priority: sprite only visible over BG color 0
                if (bgPriority && m_Framebuffer[m_LY * ScreenWidth + screenX] != 0)
                    continue;

                m_Framebuffer[m_LY * ScreenWidth + screenX] = GetColorFromPalette(palette, colorIndex);
            }
        }
    }
}

U8 PPU::GetColorFromPalette(U8 palette, U8 colorIndex)
{
    return (palette >> (colorIndex * 2)) & 0x03;
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
}
