#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>
#include <expected>

#include <types.hpp>

struct CartridgeHeader {
    std::array<U8, 4> EntryPoint;
    std::array<U8, 48> NintendoLogo;
    std::string Title;
    std::array<char, 4> ManufacturerCode;
    U8 CgbFlag;
    std::array<char, 2> NewLicenseeCode;
    U8 SgbFlag;
    U8 CartridgeType;
    U8 RomSize;
    U8 RamSize;
    U8 DestinationCode;
    U8 OldLicenseeCode;
    U8 Version;
    U8 HeaderChecksum;
    U16 GlobalChecksum;
};

enum class MBCType { None, MBC1, MBC3, MBC5 };

class Cartridge {
public:
    static std::expected<Cartridge, std::string> Load(std::string_view path);

    [[nodiscard]] const CartridgeHeader& Header() const { return m_Header; }
    [[nodiscard]] const std::vector<U8>& Data() const { return m_Data; }
    [[nodiscard]] U8 Read(U16 address) const;
    void Write(U16 address, U8 value);
    [[nodiscard]] U8 ReadRAM(U16 address) const;
    void WriteRAM(U16 address, U8 value);
    [[nodiscard]] bool ValidateLogo() const;
    [[nodiscard]] bool ValidateHeaderChecksum() const;
    [[nodiscard]] bool HasRAM() const { return m_Header.RamSize > 0; }

private:
    Cartridge() = default;

    void ParseHeader();
    void InitMBC();

    std::vector<U8> m_Data;
    std::vector<U8> m_RAM;
    CartridgeHeader m_Header;

    MBCType m_MBCType{MBCType::None};
    U16 m_RomBank{1};      // Current ROM bank (MBC5 needs 9 bits)
    U8 m_RamBank{0};       // Current RAM bank
    bool m_RamEnabled{false};
    bool m_BankingMode{false};  // MBC1: 0 = ROM mode, 1 = RAM mode
};
