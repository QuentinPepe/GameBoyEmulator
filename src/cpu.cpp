#include <cpu.hpp>
#include <print>
#include <ostream>
#include <istream>
#include <state.hpp>

CPU::CPU(Bus& bus) : m_Bus{bus}, AF{0x01B0}, BC{0x0013}, DE{0x00D8}, HL{0x014D}, SP{0xFFFE}, PC{0x0100}, IME{false}, m_EIDelay{0}, m_Halted{false}, m_HaltBug{false}
{
}

void CPU::Tick()
{
    m_Bus.Tick();
}

U8 CPU::BusRead(U16 address)
{
    m_Bus.Tick();
    return m_Bus.Read(address);
}

void CPU::BusWrite(U16 address, U8 value)
{
    m_Bus.Tick();
    m_Bus.Write(address, value);
}

U8 CPU::Fetch()
{
    U8 value = BusRead(PC);
    if (m_HaltBug)
        m_HaltBug = false;  // Don't increment PC this time
    else
        PC++;
    return value;
}

U16 CPU::Fetch16()
{
    U16 value = Fetch();
    value |= static_cast<U16>(Fetch()) << 8;
    return value;
}

void CPU::Step()
{
    if (m_Halted) {
        Tick();  // 1 M-cycle while halted
        if (m_Bus.ReadIF() & m_Bus.ReadIE() & 0x1F)
            m_Halted = false;
        else
            return;
        // Fall through to EI delay check and interrupt dispatch
    }

    // Sample effective IME before processing EI delay (matches hardware:
    // interrupt dispatch uses the IME value from before EI's toggle)
    const bool effectiveIME = IME;

    if (m_EIDelay > 0 && --m_EIDelay == 0)
        IME = true;

    if (effectiveIME) {
        const U8 IF = m_Bus.ReadIF();
        const U8 IE = m_Bus.ReadIE();

        if (const U8 pending = IF & IE & 0x1F) {
            IME = false;
            m_HaltBug = false;  // Interrupt dispatch overrides halt bug
            // Interrupt dispatch: 5 M-cycles
            Tick();  // M1: internal - recognize interrupt
            Tick();  // M2: internal - prepare SP
            BusWrite(--SP, PC >> 8);      // M3: push PC high
            BusWrite(--SP, PC & 0xFF);    // M4: push PC low
            // M5: internal - set PC, clear IF bit
            if (pending & 0x01) { PC = 0x0040; m_Bus.SetIF(IF & ~0x01); }
            else if (pending & 0x02) { PC = 0x0048; m_Bus.SetIF(IF & ~0x02); }
            else if (pending & 0x04) { PC = 0x0050; m_Bus.SetIF(IF & ~0x04); }
            else if (pending & 0x08) { PC = 0x0058; m_Bus.SetIF(IF & ~0x08); }
            else if (pending & 0x10) { PC = 0x0060; m_Bus.SetIF(IF & ~0x10); }
            Tick();  // M5: internal
            return;
        }
    }

    const U8 opcode = Fetch();  // M1: fetch opcode (1 M-cycle)

    switch (opcode)
    {
    case 0x00: // NOP (1M: fetch)
        return;
    case 0x10: // STOP (2M: fetch + fetch 0x00)
        Fetch();
        return;
    case 0x02: // LD [BC], A (2M: fetch + write)
        BusWrite(BC, A);
        return;
    case 0x07: // RLCA (1M: fetch)
        {
            const U8 carry = (A >> 7) & 1;
            A = (A << 1) | carry;
            Flags = carry << 4;
        }
        return;
    case 0x08: // LD [a16], SP (5M: fetch + fetch lo + fetch hi + write lo + write hi)
        {
            const U16 address = Fetch16();
            BusWrite(address, SP & 0xFF);
            BusWrite(address + 1, SP >> 8);
        }
        return;
    case 0x0A: // LD A, [BC] (2M: fetch + read)
        A = BusRead(BC);
        return;
    case 0x0F: // RRCA (1M: fetch)
        {
            const U8 carry = A & 1;
            A = (A >> 1) | (carry << 7);
            Flags = carry << 4;
        }
        return;
    case 0x12: // LD [DE], A (2M: fetch + write)
        BusWrite(DE, A);
        return;
    case 0x17: // RLA (1M: fetch)
        {
            const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
            const U8 newCarry = (A >> 7) & 1;
            A = (A << 1) | oldCarry;
            Flags = newCarry << 4;
        }
        return;
    case 0x18: // JR e8 (3M: fetch + fetch offset + internal)
        {
            const S8 offset = static_cast<S8>(Fetch());
            PC += offset;
            Tick();  // internal
        }
        return;
    case 0x1A: // LD A, [DE] (2M: fetch + read)
        A = BusRead(DE);
        return;
    case 0x1F: // RRA (1M: fetch)
        {
            const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
            const U8 newCarry = A & 1;
            A = (A >> 1) | (oldCarry << 7);
            Flags = newCarry << 4;
        }
        return;
    case 0x22: // LD [HL+], A (2M: fetch + write)
        BusWrite(HL++, A);
        return;
    case 0x27: // DAA (1M: fetch)
        {
            U8 correction = 0;
            bool setC = false;

            if (GetFlag(Flag::H) || (!GetFlag(Flag::N) && (A & 0x0F) > 9))
                correction |= 0x06;

            if (GetFlag(Flag::C) || (!GetFlag(Flag::N) && A > 0x99))
            {
                correction |= 0x60;
                setC = true;
            }

            A += GetFlag(Flag::N) ? -correction : correction;

            Flags = (A == 0 ? 0x80 : 0)
                  | (Flags & 0x40)
                  | (setC ? 0x10 : 0);
        }
        return;
    case 0x2A: // LD A, [HL+] (2M: fetch + read)
        A = BusRead(HL++);
        return;
    case 0x2F: // CPL (1M: fetch)
        A = ~A;
        Flags = (Flags & 0x90) | 0x60;
        return;
    case 0x32: // LD [HL-], A (2M: fetch + write)
        BusWrite(HL--, A);
        return;
    case 0x37: // SCF (1M: fetch)
        Flags = (Flags & 0x80) | 0x10;
        return;
    case 0x3A: // LD A, [HL-] (2M: fetch + read)
        A = BusRead(HL--);
        return;
    case 0x3F: // CCF (1M: fetch)
        Flags = (Flags & 0x90) ^ 0x10;
        return;
    case 0x76: // HALT (1M: fetch)
        if (m_Bus.ReadIF() & m_Bus.ReadIE() & 0x1F) {
            if (IME)
                --PC;           // PC back to HALT; interrupt dispatch will push this as return address
            else
                m_HaltBug = true;  // Halt bug: IME=0, next byte read twice
        } else {
            m_Halted = true;    // No interrupt pending: enter halt mode
        }
        return;
    case 0xC3: // JP a16 (4M: fetch + fetch lo + fetch hi + internal)
        {
            const U16 address = Fetch16();
            PC = address;
            Tick();  // internal
        }
        return;
    case 0xCB: // CB prefix
        ExecuteCB();
        return;
    case 0xC9: // RET (4M: fetch + read lo + read hi + internal)
        {
            const U8 lo = BusRead(SP++);
            const U8 hi = BusRead(SP++);
            PC = (hi << 8) | lo;
            Tick();  // internal
        }
        return;
    case 0xD9: // RETI (4M: fetch + read lo + read hi + internal)
        {
            const U8 lo = BusRead(SP++);
            const U8 hi = BusRead(SP++);
            PC = (hi << 8) | lo;
            IME = true;
            Tick();  // internal
        }
        return;
    case 0xCD: // CALL a16 (6M: fetch + fetch lo + fetch hi + internal + write hi + write lo)
        {
            const U16 address = Fetch16();
            Tick();  // internal
            BusWrite(--SP, PC >> 8);
            BusWrite(--SP, PC & 0xFF);
            PC = address;
        }
        return;
    case 0xE0: // LDH [a8], A (3M: fetch + fetch a8 + write)
        {
            const U8 offset = Fetch();
            BusWrite(0xFF00 + offset, A);
        }
        return;
    case 0xE2: // LDH [C], A (2M: fetch + write)
        BusWrite(0xFF00 + C, A);
        return;
    case 0xE8: // ADD SP, e8 (4M: fetch + fetch imm + internal + internal)
        {
            const S8 offset = static_cast<S8>(Fetch());
            const U16 result = SP + offset;
            Flags = ((SP & 0x0F) + (offset & 0x0F) > 0x0F ? 0x20 : 0)
                  | ((SP & 0xFF) + (offset & 0xFF) > 0xFF ? 0x10 : 0);
            SP = result;
            Tick();  // internal
            Tick();  // internal
        }
        return;
    case 0xE9: // JP HL (1M: fetch)
        PC = HL;
        return;
    case 0xEA: // LD [a16] A (4M: fetch + fetch lo + fetch hi + write)
        {
            const U16 address = Fetch16();
            BusWrite(address, A);
        }
        return;
    case 0xF0: // LDH A, [a8] (3M: fetch + fetch a8 + read)
        {
            const U8 offset = Fetch();
            A = BusRead(0xFF00 + offset);
        }
        return;
    case 0xF2: // LDH A, [C] (2M: fetch + read)
        A = BusRead(0xFF00 + C);
        return;
    case 0xF3: // DI (1M: fetch)
        IME = false;
        return;
    case 0xF8: // LD HL, SP+e8 (3M: fetch + fetch imm + internal)
        {
            const S8 offset = static_cast<S8>(Fetch());
            const U16 result = SP + offset;
            Flags = ((SP & 0x0F) + (offset & 0x0F) > 0x0F ? 0x20 : 0)
                  | ((SP & 0xFF) + (offset & 0xFF) > 0xFF ? 0x10 : 0);
            HL = result;
            Tick();  // internal
        }
        return;
    case 0xF9: // LD SP, HL (2M: fetch + internal)
        SP = HL;
        Tick();  // internal
        return;
    case 0xFA: // LD A, [a16] (4M: fetch + fetch lo + fetch hi + read)
        {
            const U16 address = Fetch16();
            A = BusRead(address);
        }
        return;
    case 0xFB: // EI (1M: fetch)
        m_EIDelay = 1;
        return;
    default:
        // LD r,r': opcodes 0x40-0x7F (except 0x76 = HALT)
        if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76)
        {
            const U8 dest = (opcode >> 3) & 0x07;
            const U8 src = opcode & 0x07;

            // Handle [HL] explicitly for proper timing
            U8 value;
            if (src == 6)
                value = BusRead(HL);  // 1 extra M-cycle for read
            else
                value = GetReg(src);

            if (dest == 6)
                BusWrite(HL, value);  // 1 extra M-cycle for write
            else
                SetReg(dest, value);

            return;
        }

        // INC r / DEC r
        if ((opcode & 0xC7) == 0x04) // INC r
        {
            const U8 reg = (opcode >> 3) & 0x07;
            if (reg == 6) // [HL] (3M: fetch + read + write)
            {
                U8 value = BusRead(HL);
                ++value;
                Flags = (Flags & 0x10) | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0 ? 0x20 : 0);
                BusWrite(HL, value);
            }
            else // register (1M: fetch)
            {
                U8 value = GetReg(reg);
                ++value;
                Flags = (Flags & 0x10) | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0 ? 0x20 : 0);
                SetReg(reg, value);
            }
            return;
        }
        if ((opcode & 0xC7) == 0x05) // DEC r
        {
            const U8 reg = (opcode >> 3) & 0x07;
            if (reg == 6) // [HL] (3M: fetch + read + write)
            {
                U8 value = BusRead(HL);
                --value;
                Flags = (Flags & 0x10) | 0x40 | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0x0F ? 0x20 : 0);
                BusWrite(HL, value);
            }
            else // register (1M: fetch)
            {
                U8 value = GetReg(reg);
                --value;
                Flags = (Flags & 0x10) | 0x40 | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0x0F ? 0x20 : 0);
                SetReg(reg, value);
            }
            return;
        }

        // LD r, n8 (2M: fetch + fetch imm; 3M if [HL]: fetch + fetch imm + write)
        if ((opcode & 0xC7) == 0x06)
        {
            const U8 reg = (opcode >> 3) & 0x07;
            const U8 value = Fetch();
            if (reg == 6)
                BusWrite(HL, value);
            else
                SetReg(reg, value);
            return;
        }

        // ALU A, r (1M or 2M if src=[HL])
        if (opcode >= 0x80 && opcode <= 0xBF)
        {
            const U8 op = (opcode >> 3) & 0x07;
            const U8 src = opcode & 0x07;
            const U8 value = (src == 6) ? BusRead(HL) : GetReg(src);
            switch (op)
            {
            case 0: Add(value); break;
            case 1: Adc(value); break;
            case 2: Sub(value); break;
            case 3: Sbc(value); break;
            case 4: And(value); break;
            case 5: Xor(value); break;
            case 6: Or(value); break;
            case 7: Cp(value); break;
            }
            return;
        }

        // ALU A, n8 (2M: fetch + fetch imm)
        if ((opcode & 0xC7) == 0xC6)
        {
            const U8 op = (opcode >> 3) & 0x07;
            const U8 value = Fetch();
            switch (op)
            {
            case 0: Add(value); break;
            case 1: Adc(value); break;
            case 2: Sub(value); break;
            case 3: Sbc(value); break;
            case 4: And(value); break;
            case 5: Xor(value); break;
            case 6: Or(value); break;
            case 7: Cp(value); break;
            }
            return;
        }

        // POP rr (3M: fetch + read lo + read hi)
        if ((opcode & 0xCF) == 0xC1)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U8 lo = BusRead(SP++);
            const U8 hi = BusRead(SP++);
            SetReg16(pair, (hi << 8) | lo);
            return;
        }
        // PUSH rr (4M: fetch + internal + write hi + write lo)
        if ((opcode & 0xCF) == 0xC5)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = GetReg16(pair);
            Tick();  // internal
            BusWrite(--SP, value >> 8);
            BusWrite(--SP, value & 0xFF);
            return;
        }

        // ADD HL, rr (2M: fetch + internal)
        if ((opcode & 0xCF) == 0x09)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = (pair == 3) ? SP : GetReg16(pair);
            const U32 result = HL + value;
            Flags = (Flags & 0x80)
                  | ((HL & 0x0FFF) + (value & 0x0FFF) > 0x0FFF ? 0x20 : 0)
                  | (result > 0xFFFF ? 0x10 : 0);
            HL = static_cast<U16>(result);
            Tick();  // internal
            return;
        }

        // LD rr, n16 (3M: fetch + fetch lo + fetch hi)
        if ((opcode & 0xCF) == 0x01)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = Fetch16();
            if (pair == 3)
                SP = value;
            else
                SetReg16(pair, value);
            return;
        }

        // INC rr (2M: fetch + internal)
        if ((opcode & 0xCF) == 0x03)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            ++(pair == 3 ? SP : GetReg16Ref(pair));
            Tick();  // internal
            return;
        }

        // DEC rr (2M: fetch + internal)
        if ((opcode & 0xCF) == 0x0B)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            --(pair == 3 ? SP : GetReg16Ref(pair));
            Tick();  // internal
            return;
        }

        // JR cc, e8 (2M not taken: fetch + fetch offset; 3M taken: + internal)
        if ((opcode & 0xE7) == 0x20)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const S8 offset = static_cast<S8>(Fetch());
            if (CheckCondition(cc))
            {
                PC += offset;
                Tick();  // internal (branch taken)
            }
            return;
        }

        // RET cc (2M not taken: fetch + internal; 5M taken: fetch + internal + read lo + read hi + internal)
        if ((opcode & 0xE7) == 0xC0)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            Tick();  // internal (condition eval)
            if (CheckCondition(cc))
            {
                const U8 lo = BusRead(SP++);
                const U8 hi = BusRead(SP++);
                PC = (hi << 8) | lo;
                Tick();  // internal
            }
            return;
        }

        // JP cc, a16 (3M not taken: fetch + fetch lo + fetch hi; 4M taken: + internal)
        if ((opcode & 0xE7) == 0xC2)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const U16 address = Fetch16();
            if (CheckCondition(cc))
            {
                PC = address;
                Tick();  // internal (branch taken)
            }
            return;
        }

        // CALL cc, a16 (3M not taken; 6M taken: fetch + fetch lo + fetch hi + internal + write hi + write lo)
        if ((opcode & 0xE7) == 0xC4)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const U16 address = Fetch16();
            if (CheckCondition(cc))
            {
                Tick();  // internal
                BusWrite(--SP, PC >> 8);
                BusWrite(--SP, PC & 0xFF);
                PC = address;
            }
            return;
        }

        // RST n (4M: fetch + internal + write hi + write lo)
        if ((opcode & 0xC7) == 0xC7)
        {
            const U8 target = opcode & 0x38;
            Tick();  // internal
            BusWrite(--SP, PC >> 8);
            BusWrite(--SP, PC & 0xFF);
            PC = target;
            return;
        }

        return;
    }
}

bool CPU::GetFlag(Flag flag) const
{
    return Flags & (1 << static_cast<U8>(flag));
}

void CPU::SetFlag(Flag flag, bool value)
{
    if (value)
    {
        Flags |= (1 << static_cast<U8>(flag));
    }
    else
    {
        Flags &= ~(1 << static_cast<U8>(flag));
    }
}

void CPU::DebugPrint() const
{
    std::println("CPU State:");
    std::println("  AF: 0x{:04X}  (A: 0x{:02X})", AF, A);
    std::println("  BC: 0x{:04X}  (B: 0x{:02X}, C: 0x{:02X})", BC, B, C);
    std::println("  DE: 0x{:04X}  (D: 0x{:02X}, E: 0x{:02X})", DE, D, E);
    std::println("  HL: 0x{:04X}  (H: 0x{:02X}, L: 0x{:02X})", HL, H, L);
    std::println("  SP: 0x{:04X}", SP);
    std::println("  PC: 0x{:04X}", PC);
    std::println("  Flags: Z={} N={} H={} C={}",
                 GetFlag(Flag::Z) ? 1 : 0,
                 GetFlag(Flag::N) ? 1 : 0,
                 GetFlag(Flag::H) ? 1 : 0,
                 GetFlag(Flag::C) ? 1 : 0);
}

void CPU::Inc(U8& reg)
{
    ++reg;
    Flags = (Flags & 0x10) | (reg == 0 ? 0x80 : 0) | ((reg & 0x0F) == 0 ? 0x20 : 0);
}

void CPU::Dec(U8& reg)
{
    --reg;
    Flags = (Flags & 0x10) | 0x40 | (reg == 0 ? 0x80 : 0) | ((reg & 0x0F) == 0x0F ? 0x20 : 0);
}

void CPU::Add(U8 value)
{
    const U16 result = A + value;
    Flags = ((result & 0xFF) == 0 ? 0x80 : 0)
          | ((A & 0x0F) + (value & 0x0F) > 0x0F ? 0x20 : 0)
          | (result > 0xFF ? 0x10 : 0);
    A = static_cast<U8>(result);
}

void CPU::Adc(U8 value)
{
    const U8 carry = GetFlag(Flag::C) ? 1 : 0;
    const U16 result = A + value + carry;
    Flags = ((result & 0xFF) == 0 ? 0x80 : 0)
          | ((A & 0x0F) + (value & 0x0F) + carry > 0x0F ? 0x20 : 0)
          | (result > 0xFF ? 0x10 : 0);
    A = static_cast<U8>(result);
}

void CPU::Sub(U8 value)
{
    Flags = 0x40
          | (A == value ? 0x80 : 0)
          | ((A & 0x0F) < (value & 0x0F) ? 0x20 : 0)
          | (A < value ? 0x10 : 0);
    A -= value;
}

void CPU::Sbc(U8 value)
{
    const U8 carry = GetFlag(Flag::C) ? 1 : 0;
    const S32 result = A - value - carry;
    Flags = 0x40
          | ((result & 0xFF) == 0 ? 0x80 : 0)
          | ((A & 0x0F) < (value & 0x0F) + carry ? 0x20 : 0)
          | (result < 0 ? 0x10 : 0);
    A = static_cast<U8>(result);
}

void CPU::And(U8 value)
{
    A &= value;
    Flags = (A == 0 ? 0x80 : 0) | 0x20;
}

void CPU::Or(U8 value)
{
    A |= value;
    Flags = (A == 0 ? 0x80 : 0);
}

void CPU::Xor(U8 value)
{
    A ^= value;
    Flags = (A == 0 ? 0x80 : 0);
}

void CPU::Cp(U8 value)
{
    Flags = 0x40
          | (A == value ? 0x80 : 0)
          | ((A & 0x0F) < (value & 0x0F) ? 0x20 : 0)
          | (A < value ? 0x10 : 0);
}

U8 CPU::GetReg(U8 index) const
{
    switch (index)
    {
    case 0: return B;
    case 1: return C;
    case 2: return D;
    case 3: return E;
    case 4: return H;
    case 5: return L;
    case 6: return m_Bus.Read(HL);  // Note: unticked, only for non-timing-critical paths
    case 7: return A;
    default: return 0;
    }
}

void CPU::SetReg(U8 index, U8 value)
{
    switch (index)
    {
    case 0: B = value; break;
    case 1: C = value; break;
    case 2: D = value; break;
    case 3: E = value; break;
    case 4: H = value; break;
    case 5: L = value; break;
    case 6: m_Bus.Write(HL, value); break;  // Note: unticked, only for non-timing-critical paths
    case 7: A = value; break;
    }
}

U16 CPU::GetReg16(U8 index) const
{
    switch (index)
    {
    case 0: return BC;
    case 1: return DE;
    case 2: return HL;
    case 3: return AF;
    default: return 0;
    }
}

void CPU::SetReg16(U8 index, U16 value)
{
    switch (index)
    {
    case 0: BC = value; break;
    case 1: DE = value; break;
    case 2: HL = value; break;
    case 3: AF = value & 0xFFF0; break;
    }
}

U16& CPU::GetReg16Ref(U8 index)
{
    switch (index)
    {
    case 0: return BC;
    case 1: return DE;
    case 2: return HL;
    default: return SP;
    }
}

bool CPU::CheckCondition(U8 cc) const
{
    switch (cc)
    {
    case 0: return !GetFlag(Flag::Z);
    case 1: return GetFlag(Flag::Z);
    case 2: return !GetFlag(Flag::C);
    case 3: return GetFlag(Flag::C);
    default: return false;
    }
}

void CPU::ExecuteCB()
{
    const U8 opcode = Fetch();  // M2: fetch CB opcode
    const U8 reg = opcode & 0x07;
    const U8 bit = (opcode >> 3) & 0x07;
    const U8 op = (opcode >> 6) & 0x03;
    const bool isHL = (reg == 6);

    // Read the value: from register or [HL] with ticked read
    U8 value;
    if (isHL)
        value = BusRead(HL);  // M3: read [HL]
    else
        value = GetReg(reg);

    switch (op)
    {
    case 0: // Rotates and shifts (0x00-0x3F)
        switch (bit)
        {
        case 0: // RLC
            {
                const U8 carry = (value >> 7) & 1;
                value = (value << 1) | carry;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 1: // RRC
            {
                const U8 carry = value & 1;
                value = (value >> 1) | (carry << 7);
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 2: // RL
            {
                const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
                const U8 newCarry = (value >> 7) & 1;
                value = (value << 1) | oldCarry;
                Flags = (value == 0 ? 0x80 : 0) | (newCarry << 4);
            }
            break;
        case 3: // RR
            {
                const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
                const U8 newCarry = value & 1;
                value = (value >> 1) | (oldCarry << 7);
                Flags = (value == 0 ? 0x80 : 0) | (newCarry << 4);
            }
            break;
        case 4: // SLA
            {
                const U8 carry = (value >> 7) & 1;
                value <<= 1;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 5: // SRA
            {
                const U8 carry = value & 1;
                value = (value >> 1) | (value & 0x80);
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 6: // SWAP
            value = ((value & 0x0F) << 4) | ((value >> 4) & 0x0F);
            Flags = (value == 0 ? 0x80 : 0);
            break;
        case 7: // SRL
            {
                const U8 carry = value & 1;
                value >>= 1;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        }
        // Write back
        if (isHL)
            BusWrite(HL, value);  // M4: write [HL]
        else
            SetReg(reg, value);
        break;

    case 1: // BIT (read-only, no write-back)
        Flags = (Flags & 0x10) | 0x20 | ((value & (1 << bit)) == 0 ? 0x80 : 0);
        // No write-back for BIT; [HL] is 3M total (fetch CB + fetch op + read)
        return;

    case 2: // RES
        value &= ~(1 << bit);
        if (isHL)
            BusWrite(HL, value);
        else
            SetReg(reg, value);
        break;

    case 3: // SET
        value |= (1 << bit);
        if (isHL)
            BusWrite(HL, value);
        else
            SetReg(reg, value);
        break;
    }
}

void CPU::SaveState(std::ostream& out) const
{
    state::Write(out, AF);
    state::Write(out, BC);
    state::Write(out, DE);
    state::Write(out, HL);
    state::Write(out, SP);
    state::Write(out, PC);
    state::Write(out, IME);
    state::Write(out, m_EIDelay);
    state::Write(out, m_Halted);
    state::Write(out, m_HaltBug);
}

void CPU::LoadState(std::istream& in)
{
    state::Read(in, AF);
    state::Read(in, BC);
    state::Read(in, DE);
    state::Read(in, HL);
    state::Read(in, SP);
    state::Read(in, PC);
    state::Read(in, IME);
    state::Read(in, m_EIDelay);
    state::Read(in, m_Halted);
    state::Read(in, m_HaltBug);
}
