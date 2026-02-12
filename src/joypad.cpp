#include <joypad.hpp>
#include <ostream>
#include <istream>
#include <state.hpp>

void Joypad::SaveState(std::ostream& out) const
{
    state::Write(out, m_Select);
    state::Write(out, m_Buttons);
}

void Joypad::LoadState(std::istream& in)
{
    state::Read(in, m_Select);
    state::Read(in, m_Buttons);
}
