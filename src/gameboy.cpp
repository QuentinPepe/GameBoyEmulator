#include <gameboy.hpp>
#include <fstream>
#include <print>
#include <state.hpp>

GameBoy::GameBoy(Cartridge&& cart)
    : m_Cartridge{std::move(cart)}
    , m_Timer{}
    , m_PPU{}
    , m_APU{}
    , m_Bus{m_Cartridge, m_Timer, m_PPU, m_APU}
    , m_CPU{m_Bus}
{
}

U32 GameBoy::Step()
{
    m_Bus.ResetCycleCount();
    m_CPU.Step();
    return m_Bus.GetCycleCount();
}

bool GameBoy::SaveState(std::string_view path) const
{
    std::ofstream file{std::string(path), std::ios::binary};
    if (!file) return false;

    state::Write(file, state::Magic);
    state::Write(file, state::Version);

    m_CPU.SaveState(file);
    m_Bus.SaveState(file);
    m_Timer.SaveState(file);
    m_PPU.SaveState(file);
    m_APU.SaveState(file);
    m_Cartridge.SaveState(file);

    return file.good();
}

bool GameBoy::LoadState(std::string_view path)
{
    std::ifstream file{std::string(path), std::ios::binary};
    if (!file) return false;

    U32 magic = 0;
    U8 version = 0;
    state::Read(file, magic);
    state::Read(file, version);

    if (magic != state::Magic || version != state::Version)
        return false;

    m_CPU.LoadState(file);
    m_Bus.LoadState(file);
    m_Timer.LoadState(file);
    m_PPU.LoadState(file);
    m_APU.LoadState(file);
    m_Cartridge.LoadState(file);

    return file.good();
}
