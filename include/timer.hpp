#pragma once

#include <iosfwd>
#include <optional>
#include <types.hpp>

class Timer {
public:
    Timer() = default;

    void Tick(U8 mCycles);

    [[nodiscard]] std::optional<U8> Read(U16 address) const;
    bool Write(U16 address, U8 value);

    [[nodiscard]] bool InterruptRequested();

    void SaveState(std::ostream& out) const;
    void LoadState(std::istream& in);

private:
    // Internal 16-bit counter - only upper 8 bits are exposed as DIV (0xFF04)
    U16 m_Div{};

    U8 m_TIMA{};  // 0xFF05 - Timer counter
    U8 m_TMA{};   // 0xFF06 - Timer modulo (reload value)
    U8 m_TAC{};   // 0xFF07 - Timer control

    bool m_InterruptFlag{};

    // Returns the bit position in m_Div based on TAC clock select
    // Clock select (TAC bits 1-0) -> bit position in m_Div:
    //   00 -> bit 9  (every 256 M-cycles, 4096 Hz)
    //   01 -> bit 3  (every 4 M-cycles, 262144 Hz)
    //   10 -> bit 5  (every 16 M-cycles, 65536 Hz)
    //   11 -> bit 7  (every 64 M-cycles, 16384 Hz)
    [[nodiscard]] U8 GetTimerBit() const;
};
