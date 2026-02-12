#include <bus.hpp>
#include <timer.hpp>
#include <ppu.hpp>
#include <apu.hpp>
#include <ostream>
#include <istream>
#include <state.hpp>

Bus::Bus(Cartridge& cart, Timer& timer, PPU& ppu, APU& apu, bool cgbMode)
    : m_Cartridge{cart}
    , m_Timer{timer}
    , m_PPU{ppu}
    , m_APU{apu}
    , m_CgbMode{cgbMode}
{
}

void Bus::Tick()
{
    m_CycleCount += 4;

    m_Timer.Tick(4);  // Timer always runs at CPU speed
    if (m_Timer.InterruptRequested())
        m_IoRegisters[0x0F] |= 0x04;  // Timer interrupt = bit 2

    const U8 ppuCycles = m_DoubleSpeed ? 2 : 4;  // PPU stays at 4MHz
    m_PPU.Tick(ppuCycles);
    if (m_PPU.VBlankInterruptRequested())
        m_IoRegisters[0x0F] |= 0x01;  // VBlank interrupt = bit 0
    if (m_PPU.StatInterruptRequested())
        m_IoRegisters[0x0F] |= 0x02;  // STAT interrupt = bit 1

    m_APU.Tick(ppuCycles);  // APU stays at 4MHz

    // CGB HBlank DMA: transfer 16 bytes when HBlank starts
    // Always consume the flag to prevent stale triggers
    const bool hblankStarted = m_PPU.HBlankStarted();
    if (m_HdmaActive && hblankStarted)
    {
        for (U16 i = 0; i < 16; i++)
            m_PPU.WriteVRAM(m_HdmaDst + i, Read(static_cast<U16>(m_HdmaSrc + i)));
        m_HdmaSrc += 16;
        m_HdmaDst += 16;
        if (m_HdmaLength == 0)
        {
            m_HdmaActive = false;
            m_HdmaLength = 0xFF;
        }
        else
        {
            m_HdmaLength--;
        }
    }
}

U8 Bus::Read(U16 address) const {

    if (address <= 0x7FFF) {
        return m_Cartridge.Read(address);
    }
    if (address <= 0x9FFF) {
        return m_PPU.ReadVRAM(address - 0x8000);
    }
    if (address <= 0xBFFF) {
        return m_Cartridge.ReadRAM(address);
    }
    if (address <= 0xDFFF) {
        if (m_CgbMode && address >= 0xD000)
            return m_WorkRam[m_WramBank * 0x1000 + (address - 0xD000)];
        return m_WorkRam[address - 0xC000];
    }
    if (address <= 0xFDFF) {
        U16 mirrored = address - 0x2000;
        if (m_CgbMode && mirrored >= 0xD000)
            return m_WorkRam[m_WramBank * 0x1000 + (mirrored - 0xD000)];
        return m_WorkRam[mirrored - 0xC000];
    }
    if (address <= 0xFE9F) {
        return m_PPU.ReadOAM(address - 0xFE00);
    }
    if (address <= 0xFEFF) {
        return 0xFF;
    }
    if (address <= 0xFF7F) {
        if (address == 0xFF00) return m_Joypad.Read();
        if (address == 0xFF0F) return m_IoRegisters[0x0F] | 0xE0;  // IF: bits 5-7 always read as 1
        if (address == 0xFF70 && m_CgbMode) return m_WramBank | 0xF8;
        if (address == 0xFF55 && m_CgbMode) return m_HdmaLength | (m_HdmaActive ? 0x00 : 0x80);
        if (address == 0xFF4D && m_CgbMode) return (m_DoubleSpeed ? 0x80 : 0x00) | (m_SpeedSwitch ? 0x01 : 0x00) | 0x7E;
        if (auto v = m_Timer.Read(address)) return *v;
        if (auto v = m_PPU.Read(address)) return *v;
        if (auto v = m_APU.Read(address)) return *v;
        return m_IoRegisters[address - 0xFF00];
    }
    if (address <= 0xFFFE) {
        return m_HighRam[address - 0xFF80];
    }
    return m_InterruptEnable;
}

void Bus::Write(U16 address, U8 value) {
    // Serial output: when SC (0xFF02) is written with 0x81, capture SB (0xFF01)
    if (address == 0xFF02 && value == 0x81)
    {
        const char c = static_cast<char>(m_IoRegisters[0x01]);

        m_SerialBuffer += c;
        if (m_SerialBuffer.find("Passed") != std::string::npos)
            m_TestResult = TestResult::Passed;
        else if (m_SerialBuffer.find("Failed") != std::string::npos)
            m_TestResult = TestResult::Failed;

        if (m_SerialBuffer.size() > 100)
            m_SerialBuffer = m_SerialBuffer.substr(50);
    }
    if (address <= 0x7FFF) {
        m_Cartridge.Write(address, value);
        return;
    }
    if (address <= 0x9FFF) {
        m_PPU.WriteVRAM(address - 0x8000, value);
        return;
    }
    if (address <= 0xBFFF) {
        m_Cartridge.WriteRAM(address, value);
        return;
    }
    if (address <= 0xDFFF) {
        if (m_CgbMode && address >= 0xD000)
            m_WorkRam[m_WramBank * 0x1000 + (address - 0xD000)] = value;
        else
            m_WorkRam[address - 0xC000] = value;
        return;
    }
    if (address <= 0xFDFF) {
        U16 mirrored = address - 0x2000;
        if (m_CgbMode && mirrored >= 0xD000)
            m_WorkRam[m_WramBank * 0x1000 + (mirrored - 0xD000)] = value;
        else
            m_WorkRam[mirrored - 0xC000] = value;
        return;
    }
    if (address <= 0xFE9F) {
        m_PPU.WriteOAM(address - 0xFE00, value);
        return;
    }
    if (address <= 0xFEFF) {
        return;
    }
    if (address <= 0xFF7F) {
        if (address == 0xFF00) { m_Joypad.Write(value); return; }
        if (address == 0xFF70 && m_CgbMode) {
            m_WramBank = value & 0x07;
            if (m_WramBank == 0) m_WramBank = 1;
            m_IoRegisters[0x70] = value;
            return;
        }
        if (address == 0xFF46) {
            // OAM DMA Transfer: copy 160 bytes from (value * 0x100) to OAM
            U16 src = static_cast<U16>(value) << 8;
            for (U16 i = 0; i < 160; i++) {
                m_PPU.WriteOAM(i, Read(static_cast<U16>(src + i)));
            }
            m_IoRegisters[0x46] = value;
            return;
        }
        if (m_CgbMode) {
            if (address == 0xFF4D) { m_SpeedSwitch = value & 0x01; return; }
            if (address == 0xFF51) { m_HdmaSrc = (m_HdmaSrc & 0x00FF) | (static_cast<U16>(value) << 8); return; }
            if (address == 0xFF52) { m_HdmaSrc = (m_HdmaSrc & 0xFF00) | (value & 0xF0); return; }
            if (address == 0xFF53) { m_HdmaDst = (m_HdmaDst & 0x00FF) | (static_cast<U16>(value & 0x1F) << 8); return; }
            if (address == 0xFF54) { m_HdmaDst = (m_HdmaDst & 0xFF00) | (value & 0xF0); return; }
            if (address == 0xFF55) {
                if (m_HdmaActive && !(value & 0x80)) {
                    // Writing bit 7=0 during active HBlank DMA cancels it
                    m_HdmaActive = false;
                    m_HdmaLength = value & 0x7F;
                    return;
                }
                m_HdmaLength = value & 0x7F;
                if (value & 0x80) {
                    // HBlank DMA: transfer 16 bytes per HBlank
                    m_HdmaActive = true;
                    m_HdmaMode = true;
                } else {
                    // General DMA: transfer all bytes immediately
                    m_HdmaActive = false;
                    m_HdmaMode = false;
                    U16 length = (static_cast<U16>(m_HdmaLength) + 1) * 16;
                    for (U16 i = 0; i < length; i++) {
                        m_PPU.WriteVRAM(m_HdmaDst + i, Read(static_cast<U16>(m_HdmaSrc + i)));
                    }
                    m_HdmaSrc += length;
                    m_HdmaDst += length;
                    m_HdmaLength = 0xFF;
                }
                return;
            }
        }
        if (m_Timer.Write(address, value)) return;
        if (m_PPU.Write(address, value)) return;
        if (m_APU.Write(address, value)) return;
        m_IoRegisters[address - 0xFF00] = value;
        return;
    }
    if (address <= 0xFFFE) {
        m_HighRam[address - 0xFF80] = value;
        return;
    }
    m_InterruptEnable = value;
}

void Bus::PerformSpeedSwitch()
{
    m_DoubleSpeed = !m_DoubleSpeed;
    m_SpeedSwitch = false;
    m_Timer.ResetDiv();
}

void Bus::SaveState(std::ostream& out) const
{
    state::Write(out, m_WorkRam);
    state::Write(out, m_IoRegisters);
    state::Write(out, m_HighRam);
    state::Write(out, m_InterruptEnable);
    m_Joypad.SaveState(out);
    // CGB fields
    state::Write(out, m_WramBank);
    state::Write(out, m_DoubleSpeed);
    state::Write(out, m_SpeedSwitch);
    state::Write(out, m_HdmaSrc);
    state::Write(out, m_HdmaDst);
    state::Write(out, m_HdmaLength);
    state::Write(out, m_HdmaActive);
    state::Write(out, m_HdmaMode);
}

void Bus::LoadState(std::istream& in)
{
    state::Read(in, m_WorkRam);
    state::Read(in, m_IoRegisters);
    state::Read(in, m_HighRam);
    state::Read(in, m_InterruptEnable);
    m_Joypad.LoadState(in);
    // CGB fields
    state::Read(in, m_WramBank);
    state::Read(in, m_DoubleSpeed);
    state::Read(in, m_SpeedSwitch);
    state::Read(in, m_HdmaSrc);
    state::Read(in, m_HdmaDst);
    state::Read(in, m_HdmaLength);
    state::Read(in, m_HdmaActive);
    state::Read(in, m_HdmaMode);
}
