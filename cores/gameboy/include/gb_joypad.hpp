#pragma once

#include <iosfwd>
#include <types.hpp>

namespace gb {

class Joypad {
public:
    static constexpr U8 Right  = 0x01;
    static constexpr U8 Left   = 0x02;
    static constexpr U8 Up     = 0x04;
    static constexpr U8 Down   = 0x08;
    static constexpr U8 A      = 0x10;
    static constexpr U8 B      = 0x20;
    static constexpr U8 Select = 0x40;
    static constexpr U8 Start  = 0x80;

    void Press(U8 button)   {
        m_Buttons |= button;
    }
    void Release(U8 button) {
        m_Buttons &= ~button;
    }

    // Called when game writes to 0xFF00
    void Write(U8 value) { m_Select = value; }

    // Called when game reads from 0xFF00
    [[nodiscard]] U8 Read() const {
        U8 result = 0x0F;  // All buttons released by default

        if (!(m_Select & 0x10)) {  // Direction keys selected
            if (m_Buttons & Right) result &= ~0x01;
            if (m_Buttons & Left)  result &= ~0x02;
            if (m_Buttons & Up)    result &= ~0x04;
            if (m_Buttons & Down)  result &= ~0x08;
        }

        if (!(m_Select & 0x20)) {  // Button keys selected
            if (m_Buttons & A)      result &= ~0x01;
            if (m_Buttons & B)      result &= ~0x02;
            if (m_Buttons & Select) result &= ~0x04;
            if (m_Buttons & Start)  result &= ~0x08;
        }

        // Bits 7-6 always read as 1 (unused)
        return 0xC0 | (m_Select & 0x30) | result;
    }

    void SaveState(std::ostream& out) const;
    void LoadState(std::istream& in);

private:
    U8 m_Select{0x30};   // Bits 4-5: button group select
    U8 m_Buttons{};
};

} // namespace gb
