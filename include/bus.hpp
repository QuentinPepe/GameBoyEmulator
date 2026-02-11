#pragma once

#include <array>
#include <string>

#include <types.hpp>
#include <cartridge.hpp>
#include <joypad.hpp>

class Timer;
class PPU;
class APU;

enum class TestResult { Running, Passed, Failed };

class Bus {
public:
    Bus(Cartridge& cart, Timer& timer, PPU& ppu, APU& apu);

    Joypad& GetJoypad() { return m_Joypad; }

    [[nodiscard]] U8 Read(U16 address) const;
    void Write(U16 address, U8 value);

    [[nodiscard]] U8 ReadIF() const { return m_IoRegisters[0x0F]; }
    [[nodiscard]] U8 ReadIE() const { return m_InterruptEnable; }
    void SetIF(U8 value) { m_IoRegisters[0x0F] = value; }

    [[nodiscard]] TestResult GetTestResult() const { return m_TestResult; }

private:

    Cartridge& m_Cartridge;
    Timer& m_Timer;
    PPU& m_PPU;
    APU& m_APU;
    Joypad m_Joypad;
    std::array<U8, 0x2000> m_WorkRam{};
    std::array<U8, 0x80> m_IoRegisters{};
    std::array<U8, 0x7F> m_HighRam{};
    U8 m_InterruptEnable{};

    std::string m_SerialBuffer;
    TestResult m_TestResult{TestResult::Running};
};
