#include <gb_cartridge.hpp>

#include <ctime>
#include <fstream>
#include <format>
#include <ostream>
#include <istream>
#include <state.hpp>

namespace gb {

namespace {
    constexpr U16 EntryPointOffset = 0x0100;
    constexpr U16 NintendoLogoOffset = 0x0104;
    constexpr U16 TitleOffset = 0x0134;
    constexpr U16 TitleLength = 16;
    constexpr U16 ManufacturerCodeOffset = 0x013F;
    constexpr U16 CgbFlagOffset = 0x0143;
    constexpr U16 NewLicenseeCodeOffset = 0x0144;
    constexpr U16 SgbFlagOffset = 0x0146;
    constexpr U16 CartridgeTypeOffset = 0x0147;
    constexpr U16 RomSizeOffset = 0x0148;
    constexpr U16 RamSizeOffset = 0x0149;
    constexpr U16 DestinationOffset = 0x014A;
    constexpr U16 OldLicenseeCodeOffset = 0x014B;
    constexpr U16 VersionOffset = 0x014C;
    constexpr U16 HeaderChecksumOffset = 0x014D;
    constexpr U16 GlobalChecksumOffset = 0x014E;

    constexpr std::array<U8, 48> ValidNintendoLogo = {
        0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
        0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
        0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
        0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
        0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
        0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E
    };
}

std::expected<Cartridge, std::string> Cartridge::Load(std::string_view path) {
    std::ifstream file{std::string(path), std::ios::binary};

    if (!file) {
        return std::unexpected(std::format("Failed to open ROM: {}", path));
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    Cartridge cart;
    cart.m_Data.resize(size);

    if (!file.read(reinterpret_cast<char*>(cart.m_Data.data()), size)) {
        return std::unexpected(std::format("Failed to read ROM: {}", path));
    }

    cart.m_SavePath = std::filesystem::path(path).replace_extension(".sav");
    cart.ParseHeader();
    cart.InitMBC();
    cart.LoadSaveRAM();
    return cart;
}

void Cartridge::ParseHeader() {
    for (Size i = 0; i < 4; ++i) {
        m_Header.EntryPoint[i] = m_Data[EntryPointOffset + i];
    }

    for (Size i = 0; i < 48; ++i) {
        m_Header.NintendoLogo[i] = m_Data[NintendoLogoOffset + i];
    }

    m_Header.Title.reserve(TitleLength);
    for (Size i = 0; i < TitleLength; ++i) {
        const char c = static_cast<char>(m_Data[TitleOffset + i]);
        if (c == '\0') break;
        m_Header.Title.push_back(c);
    }

    for (Size i = 0; i < 4; ++i) {
        m_Header.ManufacturerCode[i] = static_cast<char>(m_Data[ManufacturerCodeOffset + i]);
    }

    m_Header.CgbFlag = m_Data[CgbFlagOffset];

    for (Size i = 0; i < 2; ++i) {
        m_Header.NewLicenseeCode[i] = static_cast<char>(m_Data[NewLicenseeCodeOffset + i]);
    }

    m_Header.SgbFlag = m_Data[SgbFlagOffset];
    m_Header.CartridgeType = m_Data[CartridgeTypeOffset];
    m_Header.RomSize = m_Data[RomSizeOffset];
    m_Header.RamSize = m_Data[RamSizeOffset];
    m_Header.DestinationCode = m_Data[DestinationOffset];
    m_Header.OldLicenseeCode = m_Data[OldLicenseeCodeOffset];
    m_Header.Version = m_Data[VersionOffset];
    m_Header.HeaderChecksum = m_Data[HeaderChecksumOffset];
    m_Header.GlobalChecksum = static_cast<U16>(
        (m_Data[GlobalChecksumOffset] << 8) | m_Data[GlobalChecksumOffset + 1]
    );
}

void Cartridge::InitMBC() {
    switch (m_Header.CartridgeType) {
        case 0x00: m_MBCType = MBCType::None; break;
        case 0x01: case 0x02: case 0x03: m_MBCType = MBCType::MBC1; break;
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: m_MBCType = MBCType::MBC3; break;
        case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: m_MBCType = MBCType::MBC5; break;
        default: m_MBCType = MBCType::None; break;
    }

    switch (m_Header.CartridgeType) {
        case 0x03:                  // MBC1+RAM+BATTERY
        case 0x06:                  // MBC2+BATTERY
        case 0x09:                  // ROM+RAM+BATTERY
        case 0x0D:                  // MMM01+RAM+BATTERY
        case 0x0F: case 0x10:      // MBC3+TIMER+BATTERY, MBC3+TIMER+RAM+BATTERY
        case 0x13:                  // MBC3+RAM+BATTERY
        case 0x1B: case 0x1E:      // MBC5+RAM+BATTERY, MBC5+RUMBLE+RAM+BATTERY
            m_HasBattery = true;
            break;
        default:
            m_HasBattery = false;
            break;
    }

    m_HasRTC = (m_Header.CartridgeType == 0x0F || m_Header.CartridgeType == 0x10);
    if (m_HasRTC) {
        m_RTCBaseTimestamp = std::time(nullptr);
    }

    Size ramSize = 0;
    switch (m_Header.RamSize) {
        case 0x00: ramSize = 0; break;
        case 0x01: ramSize = 2 * 1024; break;      // 2 KB
        case 0x02: ramSize = 8 * 1024; break;      // 8 KB
        case 0x03: ramSize = 32 * 1024; break;     // 32 KB (4 banks)
        case 0x04: ramSize = 128 * 1024; break;    // 128 KB (16 banks)
        case 0x05: ramSize = 64 * 1024; break;     // 64 KB (8 banks)
        default: ramSize = 0; break;
    }
    m_RAM.resize(ramSize, 0);
}

U8 Cartridge::Read(U16 address) const {
    if (m_MBCType == MBCType::None) {
        if (address < m_Data.size()) {
            return m_Data[address];
        }
        return 0xFF;
    }

    // ROM Bank 0 (0x0000-0x3FFF)
    if (address <= 0x3FFF) {
        if (m_MBCType == MBCType::MBC1 && m_BankingMode && m_Data.size() > 0x100000) {
            // MBC1 Mode 1 with >1MB ROM: upper bits affect bank 0 area
            U32 bankOffset = (static_cast<U32>(m_RamBank) << 5) * 0x4000;
            U32 fullAddress = bankOffset + address;
            if (fullAddress < m_Data.size()) {
                return m_Data[fullAddress];
            }
            return 0xFF;
        }
        return m_Data[address];
    }

    // ROM Bank N (0x4000-0x7FFF)
    if (address <= 0x7FFF) {
        U32 bank = m_RomBank;

        if (m_MBCType == MBCType::MBC1 && m_Data.size() > 0x100000) {
            // MBC1 with >1MB ROM: include upper 2 bits
            bank |= (static_cast<U32>(m_RamBank) << 5);
        }

        U32 bankOffset = bank * 0x4000;
        U32 fullAddress = bankOffset + (address - 0x4000);

        // Wrap around if address exceeds ROM size
        if (fullAddress >= m_Data.size()) {
            fullAddress %= m_Data.size();
        }

        return m_Data[fullAddress];
    }

    return 0xFF;
}

void Cartridge::Write(U16 address, U8 value) {
    if (m_MBCType == MBCType::None) {
        return; // No MBC, writes to ROM area are ignored
    }

    if (m_MBCType == MBCType::MBC1) {
        // MBC1 register writes
        if (address <= 0x1FFF) {
            m_RamEnabled = (value & 0x0F) == 0x0A;
        }
        else if (address <= 0x3FFF) {
            U16 bank = value & 0x1F;
            if (bank == 0) bank = 1;
            m_RomBank = bank;
        }
        else if (address <= 0x5FFF) {
            m_RamBank = value & 0x03;
        }
        else if (address <= 0x7FFF) {
            m_BankingMode = (value & 0x01) != 0;
        }
    }
    else if (m_MBCType == MBCType::MBC3) {
        // MBC3 register writes
        if (address <= 0x1FFF) {
            m_RamEnabled = (value & 0x0F) == 0x0A;
        }
        else if (address <= 0x3FFF) {
            // ROM Bank Number (7 bits, 0x00-0x7F)
            U16 bank = value & 0x7F;
            if (bank == 0) bank = 1;
            m_RomBank = bank;
        }
        else if (address <= 0x5FFF) {
            // RAM Bank Number (0x00-0x03) or RTC Register Select (0x08-0x0C)
            m_RamBank = value;
        }
        else if (address <= 0x7FFF) {
            if (m_HasRTC && m_RTCLatchPrev == 0x00 && value == 0x01) {
                UpdateRTCRegisters();
                m_LatchedRTC = m_RTC;
            }
            m_RTCLatchPrev = value;
        }
    }
    else if (m_MBCType == MBCType::MBC5) {
        // MBC5 register writes
        if (address <= 0x1FFF) {
            m_RamEnabled = (value & 0x0F) == 0x0A;
        }
        else if (address <= 0x2FFF) {
            // ROM Bank Number - Low 8 bits
            m_RomBank = (m_RomBank & 0x100) | value;
        }
        else if (address <= 0x3FFF) {
            // ROM Bank Number - High bit
            m_RomBank = (m_RomBank & 0xFF) | (static_cast<U16>(value & 0x01) << 8);
        }
        else if (address <= 0x5FFF) {
            // RAM Bank Number (0x00-0x0F)
            m_RamBank = value & 0x0F;
        }
    }
}

U8 Cartridge::ReadRAM(U16 address) const {
    if (!m_RamEnabled || m_RAM.empty()) {
        return 0xFF;
    }

    if (m_MBCType == MBCType::MBC3 && m_RamBank >= 0x08 && m_RamBank <= 0x0C) {
        if (!m_HasRTC) return 0xFF;
        switch (m_RamBank) {
            case 0x08: return m_LatchedRTC.Seconds;
            case 0x09: return m_LatchedRTC.Minutes;
            case 0x0A: return m_LatchedRTC.Hours;
            case 0x0B: return m_LatchedRTC.DaysLow;
            case 0x0C: return m_LatchedRTC.DaysHigh;
            default:   return 0xFF;
        }
    }

    U32 offset = address - 0xA000;
    U8 ramBank = m_RamBank;

    if (m_MBCType == MBCType::MBC1) {
        // MBC1: RAM banking only in Mode 1
        if (m_BankingMode && m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x03) * 0x2000;
        }
    }
    else if (m_MBCType == MBCType::MBC3) {
        // MBC3: 4 RAM banks (0x00-0x03)
        if (m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x03) * 0x2000;
        }
    }
    else if (m_MBCType == MBCType::MBC5) {
        // MBC5: up to 16 RAM banks
        if (m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x0F) * 0x2000;
        }
    }

    if (offset >= m_RAM.size()) {
        return 0xFF;
    }

    return m_RAM[offset];
}

void Cartridge::WriteRAM(U16 address, U8 value) {
    if (!m_RamEnabled) {
        return;
    }

    if (m_MBCType == MBCType::MBC3 && m_RamBank >= 0x08 && m_RamBank <= 0x0C) {
        if (!m_HasRTC) return;
        // Sync before writing so we don't lose elapsed time
        UpdateRTCRegisters();
        switch (m_RamBank) {
            case 0x08: m_RTC.Seconds = value & 0x3F; break;
            case 0x09: m_RTC.Minutes = value & 0x3F; break;
            case 0x0A: m_RTC.Hours = value & 0x1F; break;
            case 0x0B: m_RTC.DaysLow = value; break;
            case 0x0C: m_RTC.DaysHigh = value & 0xC1; break;
        }
        // Reset base timestamp to now with current register values
        m_RTCBaseTimestamp = std::time(nullptr);
        return;
    }

    if (m_RAM.empty()) {
        return;
    }

    U32 offset = address - 0xA000;
    U8 ramBank = m_RamBank;

    if (m_MBCType == MBCType::MBC1) {
        if (m_BankingMode && m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x03) * 0x2000;
        }
    }
    else if (m_MBCType == MBCType::MBC3) {
        if (m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x03) * 0x2000;
        }
    }
    else if (m_MBCType == MBCType::MBC5) {
        if (m_RAM.size() > 0x2000) {
            offset += static_cast<U32>(ramBank & 0x0F) * 0x2000;
        }
    }

    if (offset < m_RAM.size()) {
        m_RAM[offset] = value;
    }
}

void Cartridge::UpdateRTCRegisters() {
    if (!m_HasRTC) return;

    // If halted (bit 6 of DaysHigh), don't advance
    if (m_RTC.DaysHigh & 0x40) return;

    S64 now = std::time(nullptr);
    S64 elapsed = now - m_RTCBaseTimestamp;
    if (elapsed <= 0) return;

    m_RTCBaseTimestamp = now;

    // Convert current registers to total seconds
    U16 days = (static_cast<U16>(m_RTC.DaysHigh & 0x01) << 8) | m_RTC.DaysLow;
    S64 totalSeconds = static_cast<S64>(days) * 86400
                     + static_cast<S64>(m_RTC.Hours) * 3600
                     + static_cast<S64>(m_RTC.Minutes) * 60
                     + m_RTC.Seconds
                     + elapsed;

    m_RTC.Seconds = static_cast<U8>(totalSeconds % 60);
    totalSeconds /= 60;
    m_RTC.Minutes = static_cast<U8>(totalSeconds % 60);
    totalSeconds /= 60;
    m_RTC.Hours = static_cast<U8>(totalSeconds % 24);
    totalSeconds /= 24;

    days = static_cast<U16>(totalSeconds);
    m_RTC.DaysLow = static_cast<U8>(days & 0xFF);
    m_RTC.DaysHigh = (m_RTC.DaysHigh & 0xC0) | ((days >> 8) & 0x01);

    // Day counter overflow (>511 days)
    if (days > 511) {
        m_RTC.DaysHigh |= 0x80;  // Set carry flag
        days &= 0x1FF;
        m_RTC.DaysLow = static_cast<U8>(days & 0xFF);
        m_RTC.DaysHigh = (m_RTC.DaysHigh & 0xC0) | ((days >> 8) & 0x01);
    }
}

bool Cartridge::ValidateLogo() const {
    return m_Header.NintendoLogo == ValidNintendoLogo;
}

bool Cartridge::ValidateHeaderChecksum() const {
    U8 checksum = 0;
    for (U16 address = 0x0134; address <= 0x014C; ++address) {
        checksum = checksum - m_Data[address] - 1;
    }
    return checksum == m_Header.HeaderChecksum;
}

void Cartridge::SetSavePath(std::filesystem::path path) {
    m_SavePath = std::move(path);
    LoadSaveRAM();
}

void Cartridge::LoadSaveRAM() {
    if (!m_HasBattery) return;

    std::ifstream file{m_SavePath, std::ios::binary};
    if (!file) return;

    file.seekg(0, std::ios::end);
    const auto fileSize = static_cast<Size>(file.tellg());
    file.seekg(0, std::ios::beg);

    Size expectedSize = m_RAM.size() + (m_HasRTC ? 48 : 0);
    if (fileSize != expectedSize && fileSize != m_RAM.size()) return;

    if (!m_RAM.empty()) {
        file.read(reinterpret_cast<char*>(m_RAM.data()), static_cast<std::streamsize>(m_RAM.size()));
    }

    // Load RTC state (VBA-M format: 5×4 current + 5×4 latched + 8 timestamp = 48 bytes)
    if (m_HasRTC && fileSize >= m_RAM.size() + 48) {
        auto readU32 = [&]() -> U32 {
            U32 v = 0;
            file.read(reinterpret_cast<char*>(&v), 4);
            return v;
        };

        m_RTC.Seconds  = static_cast<U8>(readU32());
        m_RTC.Minutes  = static_cast<U8>(readU32());
        m_RTC.Hours    = static_cast<U8>(readU32());
        m_RTC.DaysLow  = static_cast<U8>(readU32());
        m_RTC.DaysHigh = static_cast<U8>(readU32());

        m_LatchedRTC.Seconds  = static_cast<U8>(readU32());
        m_LatchedRTC.Minutes  = static_cast<U8>(readU32());
        m_LatchedRTC.Hours    = static_cast<U8>(readU32());
        m_LatchedRTC.DaysLow  = static_cast<U8>(readU32());
        m_LatchedRTC.DaysHigh = static_cast<U8>(readU32());

        S64 savedTimestamp = 0;
        file.read(reinterpret_cast<char*>(&savedTimestamp), 8);
        m_RTCBaseTimestamp = savedTimestamp;
    }
}

void Cartridge::SaveRAM() const {
    if (!m_HasBattery || (m_RAM.empty() && !m_HasRTC)) return;

    std::ofstream file{m_SavePath, std::ios::binary};
    if (!file) return;

    if (!m_RAM.empty()) {
        file.write(reinterpret_cast<const char*>(m_RAM.data()), static_cast<std::streamsize>(m_RAM.size()));
    }

    // Save RTC state (VBA-M format)
    if (m_HasRTC) {
        auto writeU32 = [&](U32 v) {
            file.write(reinterpret_cast<const char*>(&v), 4);
        };

        writeU32(m_RTC.Seconds);
        writeU32(m_RTC.Minutes);
        writeU32(m_RTC.Hours);
        writeU32(m_RTC.DaysLow);
        writeU32(m_RTC.DaysHigh);

        writeU32(m_LatchedRTC.Seconds);
        writeU32(m_LatchedRTC.Minutes);
        writeU32(m_LatchedRTC.Hours);
        writeU32(m_LatchedRTC.DaysLow);
        writeU32(m_LatchedRTC.DaysHigh);

        S64 timestamp = std::time(nullptr);
        file.write(reinterpret_cast<const char*>(&timestamp), 8);
    }
}

void Cartridge::SaveState(std::ostream& out) const {
    state::Write(out, m_RomBank);
    state::Write(out, m_RamBank);
    state::Write(out, m_RamEnabled);
    state::Write(out, m_BankingMode);
    state::Write(out, m_RAM);

    if (m_HasRTC) {
        state::Write(out, m_RTC.Seconds);
        state::Write(out, m_RTC.Minutes);
        state::Write(out, m_RTC.Hours);
        state::Write(out, m_RTC.DaysLow);
        state::Write(out, m_RTC.DaysHigh);
        state::Write(out, m_LatchedRTC.Seconds);
        state::Write(out, m_LatchedRTC.Minutes);
        state::Write(out, m_LatchedRTC.Hours);
        state::Write(out, m_LatchedRTC.DaysLow);
        state::Write(out, m_LatchedRTC.DaysHigh);
        state::Write(out, m_RTCBaseTimestamp);
        state::Write(out, m_RTCLatched);
        state::Write(out, m_RTCLatchPrev);
    }
}

void Cartridge::LoadState(std::istream& in) {
    state::Read(in, m_RomBank);
    state::Read(in, m_RamBank);
    state::Read(in, m_RamEnabled);
    state::Read(in, m_BankingMode);
    state::Read(in, m_RAM);

    if (m_HasRTC) {
        state::Read(in, m_RTC.Seconds);
        state::Read(in, m_RTC.Minutes);
        state::Read(in, m_RTC.Hours);
        state::Read(in, m_RTC.DaysLow);
        state::Read(in, m_RTC.DaysHigh);
        state::Read(in, m_LatchedRTC.Seconds);
        state::Read(in, m_LatchedRTC.Minutes);
        state::Read(in, m_LatchedRTC.Hours);
        state::Read(in, m_LatchedRTC.DaysLow);
        state::Read(in, m_LatchedRTC.DaysHigh);
        state::Read(in, m_RTCBaseTimestamp);
        state::Read(in, m_RTCLatched);
        state::Read(in, m_RTCLatchPrev);
    }
}

} // namespace gb
