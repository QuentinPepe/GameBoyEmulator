#pragma once

#include <string_view>
#include <cartridge.hpp>
#include <timer.hpp>
#include <ppu.hpp>
#include <apu.hpp>
#include <bus.hpp>
#include <cpu.hpp>

class GameBoy {
public:
    explicit GameBoy(Cartridge&& cart);

    U32 Step();

    [[nodiscard]] const CPU& GetCPU() const { return m_CPU; }
    [[nodiscard]] const Bus& GetBus() const { return m_Bus; }
    [[nodiscard]] Bus& GetBus() { return m_Bus; }
    [[nodiscard]] const PPU& GetPPU() const { return m_PPU; }
    [[nodiscard]] APU& GetAPU() { return m_APU; }

    [[nodiscard]] bool FrameReady() { return m_PPU.FrameReady(); }
    void SaveRAM() const { m_Cartridge.SaveRAM(); }
    bool SaveState(std::string_view path) const;
    bool LoadState(std::string_view path);

private:
    Cartridge m_Cartridge;
    Timer m_Timer;
    PPU m_PPU;
    APU m_APU;
    Bus m_Bus;
    CPU m_CPU;
};
