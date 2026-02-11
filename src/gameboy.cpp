#include <gameboy.hpp>

GameBoy::GameBoy(Cartridge&& cart)
    : m_Cartridge{std::move(cart)}
    , m_Timer{}
    , m_PPU{}
    , m_APU{}
    , m_Bus{m_Cartridge, m_Timer, m_PPU, m_APU}
    , m_CPU{m_Bus}
{
}

U8 GameBoy::Step()
{
    const U8 cycles = m_CPU.Step();

    m_Timer.Tick(cycles);
    if (m_Timer.InterruptRequested())
        m_Bus.SetIF(m_Bus.ReadIF() | 0x04);  // Timer interrupt = bit 2

    m_PPU.Tick(cycles);
    if (m_PPU.VBlankInterruptRequested())
        m_Bus.SetIF(m_Bus.ReadIF() | 0x01);  // VBlank interrupt = bit 0
    if (m_PPU.StatInterruptRequested())
        m_Bus.SetIF(m_Bus.ReadIF() | 0x02);  // STAT interrupt = bit 1

    m_APU.Tick(cycles);

    return cycles;
}
