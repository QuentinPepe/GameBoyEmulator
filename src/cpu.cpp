#include <cpu.hpp>
#include <print>

CPU::CPU(Bus& bus) : m_Bus{bus}, AF{0x01B0}, BC{0x0013}, DE{0x00D8}, HL{0x014D}, SP{0xFFFE}, PC{0x0100}, IME{false}, m_EIDelay{0}, m_Halted{false}, m_HaltBug{false}
{
}

U8 CPU::Fetch()
{
    U8 value = m_Bus.Read(PC);
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

U8 CPU::Step()
{
    if (m_Halted) {
        if (m_Bus.ReadIF() & m_Bus.ReadIE() & 0x1F)
            m_Halted = false;
        else
            return 4;
    }

    if (m_EIDelay > 0 && --m_EIDelay == 0)
        IME = true;

    if (IME) {
        const U8 IF = m_Bus.ReadIF();
        const U8 IE = m_Bus.ReadIE();

        if (const U8 pending = IF & IE & 0x1F) {
            IME = false;
            --SP;
            m_Bus.Write(SP, PC >> 8);
            --SP;
            m_Bus.Write(SP, PC & 0xFF);

            if (pending & 0x01) { PC = 0x0040; m_Bus.SetIF(IF & ~0x01); }      // VBlank
            else if (pending & 0x02) { PC = 0x0048; m_Bus.SetIF(IF & ~0x02); } // LCD STAT
            else if (pending & 0x04) { PC = 0x0050; m_Bus.SetIF(IF & ~0x04); } // Timer
            else if (pending & 0x08) { PC = 0x0058; m_Bus.SetIF(IF & ~0x08); } // Serial
            else if (pending & 0x10) { PC = 0x0060; m_Bus.SetIF(IF & ~0x10); } // Joypad

            return 20;  // Interrupt dispatch total cycles
        }
    }

    const U8 opcode = Fetch();

    switch (opcode)
    {
    case 0x00: // NOP
        return 4;
    case 0x10: // STOP - TODO: implement low-power mode / GBC speed switch
        Fetch(); // Consume the 0x00 byte following STOP
        return 4;
    case 0x02: // LD [BC], A
        m_Bus.Write(BC, A);
        return 8;
    case 0x07: // RLCA - Rotate Left Circular A
        {
            const U8 carry = (A >> 7) & 1;
            A = (A << 1) | carry;
            Flags = carry << 4;
        }
        return 4;
    case 0x08: // LD [a16], SP
        {
            const U16 address = Fetch16();
            m_Bus.Write(address, SP & 0xFF);
            m_Bus.Write(address + 1, SP >> 8);
        }
        return 20;
    case 0x0A: // LD A, [BC]
        A = m_Bus.Read(BC);
        return 8;
    case 0x0F: // RRCA - Rotate Right Circular A
        {
            const U8 carry = A & 1;
            A = (A >> 1) | (carry << 7);
            Flags = carry << 4;
        }
        return 4;
    case 0x12: // LD [DE], A
        m_Bus.Write(DE, A);
        return 8;
    case 0x17: // RLA - Rotate Left A through carry
        {
            const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
            const U8 newCarry = (A >> 7) & 1;
            A = (A << 1) | oldCarry;
            Flags = newCarry << 4;
        }
        return 4;
    case 0x18: // JR e8
        PC += static_cast<S8>(Fetch());
        return 12;
    case 0x1A: // LD A, [DE]
        A = m_Bus.Read(DE);
        return 8;
    case 0x1F: // RRA - Rotate Right A through carry
        {
            const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
            const U8 newCarry = A & 1;
            A = (A >> 1) | (oldCarry << 7);
            Flags = newCarry << 4;
        }
        return 4;
    case 0x22: // LD [HL+], A
        m_Bus.Write(HL++, A);
        return 8;
    case 0x27: // DAA - Decimal Adjust Accumulator (BCD correction)
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
                  | (Flags & 0x40)  // Keep N
                  | (setC ? 0x10 : 0);
        }
        return 4;
    case 0x2A: // LD A, [HL+]
        A = m_Bus.Read(HL++);
        return 8;
    case 0x2F: // CPL - Complement A
        A = ~A;
        Flags = (Flags & 0x90) | 0x60; // Keep Z and C, set N and H
        return 4;
    case 0x32: // LD [HL-], A
        m_Bus.Write(HL--, A);
        return 8;
    case 0x37: // SCF - Set Carry Flag
        Flags = (Flags & 0x80) | 0x10; // Keep Z, clear N and H, set C
        return 4;
    case 0x3A: // LD A, [HL-]
        A = m_Bus.Read(HL--);
        return 8;
    case 0x3F: // CCF - Complement Carry Flag
        Flags = (Flags & 0x90) ^ 0x10; // Keep Z, clear N and H, flip C
        return 4;
    case 0x76: // HALT - Wait for interrupt (use direct register access)
        if (!IME && (m_Bus.ReadIF() & m_Bus.ReadIE() & 0x1F))
            m_HaltBug = true;  // HALT bug: PC won't increment for next fetch
        else
            m_Halted = true;
        return 4;
    case 0xC3: // JP a16
        PC = Fetch16();
        return 16;
    case 0xCB: // CB prefix
        return ExecuteCB();
    case 0xC9: // RET
        {
            const U8 lo = m_Bus.Read(SP++);
            const U8 hi = m_Bus.Read(SP++);
            PC = (hi << 8) | lo;
            return 16;
        }
    case 0xD9: // RETI - Return from interrupt
        {
            const U8 lo = m_Bus.Read(SP++);
            const U8 hi = m_Bus.Read(SP++);
            PC = (hi << 8) | lo;
            IME = true;
            return 16;
        }
    case 0xCD: // CALL a16
        {
            const U16 address = Fetch16();
            --SP;
            m_Bus.Write(SP, PC >> 8);
            --SP;
            m_Bus.Write(SP, PC & 0xFF);
            PC = address;
            return 24;
        }
    case 0xE0: // LDH [a8], A
        m_Bus.Write(0xFF00 + Fetch(), A);
        return 12;
    case 0xE2: // LDH [C], A
        m_Bus.Write(0xFF00 + C, A);
        return 8;
    case 0xE8: // ADD SP, e8
        {
            const S8 offset = static_cast<S8>(Fetch());
            const U16 result = SP + offset;
            Flags = ((SP & 0x0F) + (offset & 0x0F) > 0x0F ? 0x20 : 0)
                  | ((SP & 0xFF) + (offset & 0xFF) > 0xFF ? 0x10 : 0);
            SP = result;
        }
        return 16;
    case 0xE9: // JP HL
        PC = HL;
        return 4;
    case 0xEA: // LD [a16] A
        m_Bus.Write(Fetch16(), A);
        return 16;
    case 0xF0: // LDH A, [a8]
        A = m_Bus.Read(0xFF00 + Fetch());
        return 12;
    case 0xF2: // LDH A, [C]
        A = m_Bus.Read(0xFF00 + C);
        return 8;
    case 0xF3: // DI
        IME = false;
        return 4;
    case 0xF8: // LD HL, SP+e8
        {
            const S8 offset = static_cast<S8>(Fetch());
            const U16 result = SP + offset;
            Flags = ((SP & 0x0F) + (offset & 0x0F) > 0x0F ? 0x20 : 0)
                  | ((SP & 0xFF) + (offset & 0xFF) > 0xFF ? 0x10 : 0);
            HL = result;
        }
        return 12;
    case 0xF9: // LD SP, HL
        SP = HL;
        return 8;
    case 0xFA: // LD A, [a16]
        A = m_Bus.Read(Fetch16());
        return 16;
    case 0xFB: // EI - Enable Interrupts (delayed by one instruction)
        m_EIDelay = 2;
        return 4;
    default:
        // LD r,r': opcodes 0x40-0x7F (except 0x76 = HALT)
        // Binary format: 01 DDD SSS
        //   DDD = destination register (bits 5-3)
        //   SSS = source register (bits 2-0)
        if (opcode >= 0x40 && opcode <= 0x7F && opcode != 0x76)
        {
            const U8 dest = (opcode >> 3) & 0x07;
            const U8 src = opcode & 0x07;
            SetReg(dest, GetReg(src));
            return (dest == 6 || src == 6) ? 8 : 4;
        }

        // INC r: opcodes 0x04, 0x0C, 0x14, 0x1C, 0x24, 0x2C, 0x34, 0x3C
        // DEC r: opcodes 0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D
        // Binary format: 00 RRR 10X (X=0 for INC, X=1 for DEC)
        if ((opcode & 0xC7) == 0x04) // INC r
        {
            const U8 reg = (opcode >> 3) & 0x07;
            if (reg == 6) // [HL]
            {
                U8 value = m_Bus.Read(HL);
                ++value;
                Flags = (Flags & 0x10) | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0 ? 0x20 : 0);
                m_Bus.Write(HL, value);
                return 12;
            }
            U8 value = GetReg(reg);
            ++value;
            Flags = (Flags & 0x10) | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0 ? 0x20 : 0);
            SetReg(reg, value);
            return 4;
        }
        if ((opcode & 0xC7) == 0x05) // DEC r
        {
            const U8 reg = (opcode >> 3) & 0x07;
            if (reg == 6) // [HL]
            {
                U8 value = m_Bus.Read(HL);
                --value;
                Flags = (Flags & 0x10) | 0x40 | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0x0F ? 0x20 : 0);
                m_Bus.Write(HL, value);
                return 12;
            }
            U8 value = GetReg(reg);
            --value;
            Flags = (Flags & 0x10) | 0x40 | (value == 0 ? 0x80 : 0) | ((value & 0x0F) == 0x0F ? 0x20 : 0);
            SetReg(reg, value);
            return 4;
        }

        // LD r, n8: opcodes 0x06, 0x0E, 0x16, 0x1E, 0x26, 0x2E, 0x36, 0x3E
        // Binary format: 00 RRR 110
        if ((opcode & 0xC7) == 0x06)
        {
            const U8 reg = (opcode >> 3) & 0x07;
            const U8 value = Fetch();
            SetReg(reg, value);
            return (reg == 6) ? 12 : 8;
        }

        // ALU A, r: opcodes 0x80-0xBF
        // Binary format: 10 OOO SSS
        //   OOO = operation (0=ADD, 1=ADC, 2=SUB, 3=SBC, 4=AND, 5=XOR, 6=OR, 7=CP)
        //   SSS = source register
        if (opcode >= 0x80 && opcode <= 0xBF)
        {
            const U8 op = (opcode >> 3) & 0x07;
            const U8 src = opcode & 0x07;
            const U8 value = GetReg(src);
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
            return (src == 6) ? 8 : 4;
        }

        // ALU A, n8: opcodes 0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE
        // Binary format: 11 OOO 110
        //   OOO = operation (same as ALU A, r)
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
            return 8;
        }

        // PUSH/POP rr: opcodes 0xC1/0xC5, 0xD1/0xD5, 0xE1/0xE5, 0xF1/0xF5
        // Binary format: 11 PP 0001 (POP) or 11 PP 0101 (PUSH)
        //   PP = register pair (0=BC, 1=DE, 2=HL, 3=AF)
        if ((opcode & 0xCF) == 0xC1) // POP
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U8 lo = m_Bus.Read(SP++);
            const U8 hi = m_Bus.Read(SP++);
            SetReg16(pair, (hi << 8) | lo);
            return 12;
        }
        if ((opcode & 0xCF) == 0xC5) // PUSH
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = GetReg16(pair);
            --SP;
            m_Bus.Write(SP, value >> 8);
            --SP;
            m_Bus.Write(SP, value & 0xFF);
            return 16;
        }

        // ADD HL, rr: opcodes 0x09, 0x19, 0x29, 0x39
        // Binary format: 00 PP 1001
        if ((opcode & 0xCF) == 0x09)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = (pair == 3) ? SP : GetReg16(pair);
            const U32 result = HL + value;
            Flags = (Flags & 0x80) // Z not affected
                  | ((HL & 0x0FFF) + (value & 0x0FFF) > 0x0FFF ? 0x20 : 0) // H
                  | (result > 0xFFFF ? 0x10 : 0); // C
            HL = static_cast<U16>(result);
            return 8;
        }

        // LD rr, n16: opcodes 0x01, 0x11, 0x21, 0x31
        // Binary format: 00 PP 0001
        if ((opcode & 0xCF) == 0x01)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            const U16 value = Fetch16();
            if (pair == 3)
                SP = value;
            else
                SetReg16(pair, value);
            return 12;
        }

        // INC rr: opcodes 0x03, 0x13, 0x23, 0x33
        // Binary format: 00 PP 0011
        if ((opcode & 0xCF) == 0x03)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            ++(pair == 3 ? SP : GetReg16Ref(pair));
            return 8;
        }

        // DEC rr: opcodes 0x0B, 0x1B, 0x2B, 0x3B
        // Binary format: 00 PP 1011
        if ((opcode & 0xCF) == 0x0B)
        {
            const U8 pair = (opcode >> 4) & 0x03;
            --(pair == 3 ? SP : GetReg16Ref(pair));
            return 8;
        }

        // JR cc, e8: opcodes 0x20, 0x28, 0x30, 0x38
        // Binary format: 001 CC 000
        if ((opcode & 0xE7) == 0x20)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const S8 offset = static_cast<S8>(Fetch());
            if (CheckCondition(cc))
            {
                PC += offset;
                return 12;
            }
            return 8;
        }

        // RET cc: opcodes 0xC0, 0xC8, 0xD0, 0xD8
        // Binary format: 110 CC 000
        if ((opcode & 0xE7) == 0xC0)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            if (CheckCondition(cc))
            {
                const U8 lo = m_Bus.Read(SP++);
                const U8 hi = m_Bus.Read(SP++);
                PC = (hi << 8) | lo;
                return 20;
            }
            return 8;
        }

        // JP cc, a16: opcodes 0xC2, 0xCA, 0xD2, 0xDA
        // Binary format: 110 CC 010
        if ((opcode & 0xE7) == 0xC2)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const U16 address = Fetch16();
            if (CheckCondition(cc))
            {
                PC = address;
                return 16;
            }
            return 12;
        }

        // CALL cc, a16: opcodes 0xC4, 0xCC, 0xD4, 0xDC
        // Binary format: 110 CC 100
        if ((opcode & 0xE7) == 0xC4)
        {
            const U8 cc = (opcode >> 3) & 0x03;
            const U16 address = Fetch16();
            if (CheckCondition(cc))
            {
                --SP;
                m_Bus.Write(SP, PC >> 8);
                --SP;
                m_Bus.Write(SP, PC & 0xFF);
                PC = address;
                return 24;
            }
            return 12;
        }

        // RST n: opcodes 0xC7, 0xCF, 0xD7, 0xDF, 0xE7, 0xEF, 0xF7, 0xFF
        // Binary format: 11 TTT 111 (target = TTT * 8)
        if ((opcode & 0xC7) == 0xC7)
        {
            const U8 target = opcode & 0x38; // TTT * 8
            --SP;
            m_Bus.Write(SP, PC >> 8);
            --SP;
            m_Bus.Write(SP, PC & 0xFF);
            PC = target;
            return 16;
        }

        //std::println("Unknown opcode: 0x{:02X}", opcode);
        return 0;
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
    case 6: return m_Bus.Read(HL);
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
    case 6: m_Bus.Write(HL, value); break;
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
    case 3: AF = value & 0xFFF0; break; // Lower 4 bits of F are always 0
    }
}

U16& CPU::GetReg16Ref(U8 index)
{
    switch (index)
    {
    case 0: return BC;
    case 1: return DE;
    case 2: return HL;
    default: return SP; // index 3 = SP for INC/DEC rr
    }
}

bool CPU::CheckCondition(U8 cc) const
{
    // cc: 0=NZ, 1=Z, 2=NC, 3=C
    switch (cc)
    {
    case 0: return !GetFlag(Flag::Z);
    case 1: return GetFlag(Flag::Z);
    case 2: return !GetFlag(Flag::C);
    case 3: return GetFlag(Flag::C);
    default: return false;
    }
}

U8 CPU::ExecuteCB()
{
    const U8 opcode = Fetch();
    const U8 reg = opcode & 0x07;
    const U8 bit = (opcode >> 3) & 0x07;
    const U8 op = (opcode >> 6) & 0x03;

    U8 value = GetReg(reg);
    const bool isHL = (reg == 6);

    switch (op)
    {
    case 0: // Rotates and shifts (0x00-0x3F)
        switch (bit)
        {
        case 0: // RLC - Rotate Left Circular
            {
                const U8 carry = (value >> 7) & 1;
                value = (value << 1) | carry;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 1: // RRC - Rotate Right Circular
            {
                const U8 carry = value & 1;
                value = (value >> 1) | (carry << 7);
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 2: // RL - Rotate Left through Carry
            {
                const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
                const U8 newCarry = (value >> 7) & 1;
                value = (value << 1) | oldCarry;
                Flags = (value == 0 ? 0x80 : 0) | (newCarry << 4);
            }
            break;
        case 3: // RR - Rotate Right through Carry
            {
                const U8 oldCarry = GetFlag(Flag::C) ? 1 : 0;
                const U8 newCarry = value & 1;
                value = (value >> 1) | (oldCarry << 7);
                Flags = (value == 0 ? 0x80 : 0) | (newCarry << 4);
            }
            break;
        case 4: // SLA - Shift Left Arithmetic
            {
                const U8 carry = (value >> 7) & 1;
                value <<= 1;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 5: // SRA - Shift Right Arithmetic (preserves sign)
            {
                const U8 carry = value & 1;
                value = (value >> 1) | (value & 0x80);
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        case 6: // SWAP - Swap nibbles
            value = ((value & 0x0F) << 4) | ((value >> 4) & 0x0F);
            Flags = (value == 0 ? 0x80 : 0);
            break;
        case 7: // SRL - Shift Right Logical
            {
                const U8 carry = value & 1;
                value >>= 1;
                Flags = (value == 0 ? 0x80 : 0) | (carry << 4);
            }
            break;
        }
        SetReg(reg, value);
        break;

    case 1: // BIT - Test bit (0x40-0x7F)
        Flags = (Flags & 0x10) | 0x20 | ((value & (1 << bit)) == 0 ? 0x80 : 0);
        return isHL ? 12 : 8; // BIT doesn't write back

    case 2: // RES - Reset bit (0x80-0xBF)
        value &= ~(1 << bit);
        SetReg(reg, value);
        break;

    case 3: // SET - Set bit (0xC0-0xFF)
        value |= (1 << bit);
        SetReg(reg, value);
        break;
    }

    return isHL ? 16 : 8;
}
