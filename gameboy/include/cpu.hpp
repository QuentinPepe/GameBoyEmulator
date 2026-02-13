#pragma once

#include <iosfwd>
#include <types.hpp>
#include <bus.hpp>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4201)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

enum class Flag : U8 {
    Z = 7,
    N = 6,
    H = 5,
    C = 4
};

class CPU {
public:
    explicit CPU(Bus& bus, bool cgbMode = false);

    void Step();

    [[nodiscard]] bool GetFlag(Flag flag) const;
    void SetFlag(Flag flag, bool value);

    void DebugPrint() const;

    void SaveState(std::ostream& out) const;
    void LoadState(std::istream& in);

    union { U16 AF; struct { U8 Flags; U8 A; }; };
    union { U16 BC; struct { U8 C; U8 B; }; };
    union { U16 DE; struct { U8 E; U8 D; }; };
    union { U16 HL; struct { U8 L; U8 H; }; };
    U16 SP;
    U16 PC;
    bool IME;

private:
    Bus& m_Bus;
    bool m_CgbMode;
    U8 m_EIDelay;   // Delayed IME enable (EI takes effect after next instruction)
    bool m_Halted;  // CPU is halted, waiting for interrupt
    bool m_HaltBug; // HALT bug: next opcode byte is read twice (PC not incremented)

    void Tick();                              // 1 M-cycle internal delay
    U8 BusRead(U16 address);                  // Read + tick (1 M-cycle)
    void BusWrite(U16 address, U8 value);     // Write + tick (1 M-cycle)
    U8 Fetch();
    U16 Fetch16();

    void Inc(U8& reg);
    void Dec(U8& reg);
    void Add(U8 value);
    void Adc(U8 value);
    void Sub(U8 value);
    void Sbc(U8 value);
    void And(U8 value);
    void Or(U8 value);
    void Xor(U8 value);
    void Cp(U8 value);

    U8 GetReg(U8 index) const;
    void SetReg(U8 index, U8 value);
    U16 GetReg16(U8 index) const;
    void SetReg16(U8 index, U16 value);
    U16& GetReg16Ref(U8 index);
    bool CheckCondition(U8 cc) const;
    void ExecuteCB();
};

#ifdef _MSC_VER
#pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
