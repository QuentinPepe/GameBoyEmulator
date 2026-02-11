#include <array>
#include <timer.hpp>

void Timer::Tick(U8 mCycles)
{
    for (U8 i = 0; i < mCycles; ++i)
    {
        const bool oldBit = (m_Div >> GetTimerBit()) & 1;
        ++m_Div;
        const bool newBit = (m_Div >> GetTimerBit()) & 1;
        if (oldBit && !newBit && (m_TAC & 0x04))
        {
            ++m_TIMA;
            if (m_TIMA == 0)
            {
                m_TIMA = m_TMA;
                m_InterruptFlag = true;
            }
        }
    }
}

std::optional<U8> Timer::Read(U16 address) const
{
    switch (address)
    {
    case 0xFF04:
        return m_Div >> 8;
    case 0xFF05:
        return m_TIMA;
    case 0xFF06:
        return m_TMA;
    case 0xFF07:
        return m_TAC;
    default:
        return std::nullopt;
    }
}

bool Timer::Write(U16 address, U8 value)
{
    switch (address)
    {
    case 0xFF04:
        {
            // Falling edge detection: if selected bit was 1, it becomes 0
            const bool oldBit = (m_Div >> GetTimerBit()) & 1;
            const bool enabled = m_TAC & 0x04;

            m_Div = 0;

            if (enabled && oldBit) {
                if (++m_TIMA == 0) {
                    m_TIMA = m_TMA;
                    m_InterruptFlag = true;
                }
            }
            return true;
        }
    case 0xFF05:
        m_TIMA = value;
        return true;
    case 0xFF06:
        m_TMA = value;
        return true;
    case 0xFF07:
        {
            const bool oldEnable = m_TAC & 0x04;
            const bool oldBit = (m_Div >> GetTimerBit()) & 1;

            m_TAC = value & 0x07;

            const bool newEnable = m_TAC & 0x04;
            const bool newBit = (m_Div >> GetTimerBit()) & 1;

            // Falling edge: was high (enabled AND bit=1), now low (disabled OR bit=0)
            if ((oldEnable && oldBit) && !(newEnable && newBit)) {
                if (++m_TIMA == 0) {
                    m_TIMA = m_TMA;
                    m_InterruptFlag = true;
                }
            }
            return true;
        }
    default:
        return false;
    }
}

bool Timer::InterruptRequested()
{
    const bool currentInterruptFlag = m_InterruptFlag;
    m_InterruptFlag = false;
    return currentInterruptFlag;
}

U8 Timer::GetTimerBit() const
{
    constexpr std::array<U8, 4> BitPositions{9, 3, 5, 7};
    return BitPositions[m_TAC & 0x03];
}
