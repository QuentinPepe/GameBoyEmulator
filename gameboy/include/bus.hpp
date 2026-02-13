#pragma once

#include <array>
#include <iosfwd>
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
    Bus(Cartridge& cart, Timer& timer, PPU& ppu, APU& apu, bool cgbMode = false);

    Joypad& GetJoypad() { return m_Joypad; }

    [[nodiscard]] U8 Read(U16 address) const;
    void Write(U16 address, U8 value);

    void Tick();  // Advance 1 M-cycle (4 T-cycles): ticks Timer, PPU, APU, handles interrupts
    [[nodiscard]] U32 GetCycleCount() const { return m_CycleCount; }
    void ResetCycleCount() { m_CycleCount = 0; }

    [[nodiscard]] U8 ReadIF() const { return m_IoRegisters[0x0F]; }
    [[nodiscard]] U8 ReadIE() const { return m_InterruptEnable; }
    void SetIF(U8 value) { m_IoRegisters[0x0F] = value; }

    [[nodiscard]] TestResult GetTestResult() const { return m_TestResult; }

    [[nodiscard]] bool IsDoubleSpeed() const { return m_DoubleSpeed; }
    [[nodiscard]] bool IsSpeedSwitchArmed() const { return m_SpeedSwitch; }
    void PerformSpeedSwitch();

    void SaveState(std::ostream& out) const;
    void LoadState(std::istream& in);

private:

    Cartridge& m_Cartridge;
    Timer& m_Timer;
    PPU& m_PPU;
    APU& m_APU;
    Joypad m_Joypad;
    std::array<U8, 0x8000> m_WorkRam{};  // 32KB: 8 banks of 4KB (CGB), only first 8KB used in DMG
    U8 m_WramBank{1};  // SVBK register (0xFF70), banks 1-7 for 0xD000-0xDFFF
    std::array<U8, 0x80> m_IoRegisters{};
    std::array<U8, 0x7F> m_HighRam{};
    U8 m_InterruptEnable{};
    U32 m_CycleCount{};

    bool m_CgbMode{false};

    // CGB double speed
    bool m_DoubleSpeed{false};
    bool m_SpeedSwitch{false};

    // CGB HDMA
    U16 m_HdmaSrc{};
    U16 m_HdmaDst{};
    U8 m_HdmaLength{0xFF};
    bool m_HdmaActive{false};
    bool m_HdmaMode{false};  // false=General DMA, true=HBlank DMA

    // Serial transfer
    bool m_SerialTransferring{false};
    U16 m_SerialCycles{0};

    std::string m_SerialBuffer;
    TestResult m_TestResult{TestResult::Running};
};
