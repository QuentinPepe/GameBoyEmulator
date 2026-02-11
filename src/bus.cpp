#include <bus.hpp>
#include <timer.hpp>
#include <ppu.hpp>
#include <apu.hpp>
#include <print>

Bus::Bus(Cartridge& cart, Timer& timer, PPU& ppu, APU& apu)
    : m_Cartridge{cart}
    , m_Timer{timer}
    , m_PPU{ppu}
    , m_APU{apu}
{
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
        return m_WorkRam[address - 0xC000];
    }
    if (address <= 0xFDFF) {
        return m_WorkRam[address - 0xE000];
    }
    if (address <= 0xFE9F) {
        return m_PPU.ReadOAM(address - 0xFE00);
    }
    if (address <= 0xFEFF) {
        return 0xFF;
    }
    if (address <= 0xFF7F) {
        if (address == 0xFF00) return m_Joypad.Read();
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
        m_WorkRam[address - 0xC000] = value;
        return;
    }
    if (address <= 0xFDFF) {
        m_WorkRam[address - 0xE000] = value;
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
        if (address == 0xFF46) {
            // OAM DMA Transfer: copy 160 bytes from (value * 0x100) to OAM
            U16 src = static_cast<U16>(value) << 8;
            for (U16 i = 0; i < 160; i++) {
                m_PPU.WriteOAM(i, Read(static_cast<U16>(src + i)));
            }
            m_IoRegisters[0x46] = value;
            return;
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
