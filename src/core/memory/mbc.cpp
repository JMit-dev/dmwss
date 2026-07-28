#include "mbc.hpp"
#include "../../machine/savestate.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstring>

// External RAM size from cartridge header byte 0x149. Some carts
// (including blargg's test ROMs) declare no RAM but still use it, so
// allocate at least one 8KB bank as a floor.
static size_t RAMSizeFromHeader(const u8* rom_data, size_t rom_size) {
    size_t declared = 0;
    if (rom_size >= 0x150) {
        switch (rom_data[0x149]) {
            case 0x01: declared = 2 * 1024; break;
            case 0x02: declared = 8 * 1024; break;
            case 0x03: declared = 32 * 1024; break;
            case 0x04: declared = 128 * 1024; break;
            case 0x05: declared = 64 * 1024; break;
            default: break;
        }
    }
    return declared > 8 * 1024 ? declared : 8 * 1024;
}

std::unique_ptr<MBC> MBC::Create(u8 cartridge_type, const u8* rom_data, size_t rom_size) {
    switch (cartridge_type) {
        case 0x00:  // ROM ONLY
            return std::make_unique<MBC0>(rom_data, rom_size);

        case 0x01:  // MBC1
        case 0x02:  // MBC1+RAM
        case 0x03:  // MBC1+RAM+BATTERY
            return std::make_unique<MBC1>(rom_data, rom_size);

        case 0x0F:  // MBC3+TIMER+BATTERY
        case 0x10:  // MBC3+TIMER+RAM+BATTERY
        case 0x11:  // MBC3
        case 0x12:  // MBC3+RAM
        case 0x13:  // MBC3+RAM+BATTERY
            return std::make_unique<MBC3>(rom_data, rom_size, cartridge_type == 0x0F || cartridge_type == 0x10);

        case 0x19:  // MBC5
        case 0x1A:  // MBC5+RAM
        case 0x1B:  // MBC5+RAM+BATTERY
        case 0x1C:  // MBC5+RUMBLE
        case 0x1D:  // MBC5+RUMBLE+RAM
        case 0x1E:  // MBC5+RUMBLE+RAM+BATTERY
            return std::make_unique<MBC5>(rom_data, rom_size);

        default:
            spdlog::error("Unsupported cartridge type: 0x{:02X}", cartridge_type);
            return nullptr;
    }
}

// ============================================================================
// MBC0 Implementation (No banking, simple 32KB ROM)
// ============================================================================

MBC0::MBC0(const u8* rom_data, size_t rom_size) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);
    spdlog::info("MBC0 initialized with ROM size: {} bytes", rom_size);
}

u8 MBC0::Read(u16 address) const {
    if (address < m_rom.size()) {
        return m_rom[address];
    }
    return 0xFF;
}

void MBC0::Write(u16 address, u8 value) {
    // ROM writes are ignored in MBC0
    (void)address;
    (void)value;
}

u8 MBC0::ReadRAM(u16 address) const {
    // No RAM in MBC0
    (void)address;
    return 0xFF;
}

void MBC0::WriteRAM(u16 address, u8 value) {
    // No RAM in MBC0
    (void)address;
    (void)value;
}

bool MBC0::SaveRAM(const std::string& path) {
    (void)path;
    return true;  // No RAM to save
}

bool MBC0::LoadRAM(const std::string& path) {
    (void)path;
    return true;  // No RAM to load
}

// ============================================================================
// MBC1 Implementation
// ============================================================================

MBC1::MBC1(const u8* rom_data, size_t rom_size) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);

    // Allocate RAM per the cartridge header so .sav files match the cart
    m_ram.resize(RAMSizeFromHeader(rom_data, rom_size), 0);

    spdlog::info("MBC1 initialized with ROM size: {} bytes, RAM: {} bytes",
                 rom_size, m_ram.size());
}

u32 MBC1::GetROMBankOffset() const {
    if (!m_banking_mode) {
        // ROM banking mode: use full 5-bit bank number
        u8 bank = m_rom_bank & 0x1F;
        if (bank == 0) bank = 1;  // Bank 0 redirects to bank 1
        return bank * 0x4000;
    } else {
        // RAM banking mode: only lower 5 bits used
        u8 bank = m_rom_bank & 0x1F;
        if (bank == 0) bank = 1;
        return bank * 0x4000;
    }
}

u32 MBC1::GetRAMBankOffset() const {
    if (m_banking_mode) {
        // RAM banking mode: use RAM bank
        return (m_ram_bank & 0x03) * 0x2000;
    }
    return 0;  // Always bank 0 in ROM banking mode
}

u8 MBC1::Read(u16 address) const {
    if (address <= 0x3FFF) {
        // ROM Bank 0
        return m_rom[address];
    } else if (address <= 0x7FFF) {
        // ROM Bank 1-127 (switchable)
        u32 offset = GetROMBankOffset() + (address - 0x4000);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
    }
    return 0xFF;
}

void MBC1::Write(u16 address, u8 value) {
    if (address <= 0x1FFF) {
        // RAM Enable
        m_ram_enabled = (value & 0x0F) == 0x0A;
    } else if (address <= 0x3FFF) {
        // ROM Bank Number (lower 5 bits)
        m_rom_bank = value & 0x1F;
        if (m_rom_bank == 0) m_rom_bank = 1;
    } else if (address <= 0x5FFF) {
        // RAM Bank Number or upper bits of ROM Bank Number
        m_ram_bank = value & 0x03;
    } else if (address <= 0x7FFF) {
        // Banking Mode Select
        m_banking_mode = (value & 0x01) != 0;
    }
}

u8 MBC1::ReadRAM(u16 address) const {
    if (!m_ram_enabled) return 0xFF;

    u32 offset = GetRAMBankOffset() + (address - 0xA000);
    if (offset < m_ram.size()) {
        return m_ram[offset];
    }
    return 0xFF;
}

void MBC1::WriteRAM(u16 address, u8 value) {
    if (!m_ram_enabled) return;

    u32 offset = GetRAMBankOffset() + (address - 0xA000);
    if (offset < m_ram.size()) {
        m_ram[offset] = value;
    }
}

bool MBC1::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());
    return file.good();
}

bool MBC1::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    return file.good();
}

// ============================================================================
// MBC3 Implementation (with RTC support)
// ============================================================================

MBC3::MBC3(const u8* rom_data, size_t rom_size, bool has_rtc)
    : m_has_rtc(has_rtc) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);

    // Allocate RAM per the cartridge header so .sav files match the cart
    m_ram.resize(RAMSizeFromHeader(rom_data, rom_size), 0);

    spdlog::info("MBC3 initialized with ROM size: {} bytes, RAM: {} bytes, RTC: {}",
                 rom_size, m_ram.size(), has_rtc);
}

u32 MBC3::GetROMBankOffset() const {
    u8 bank = m_rom_bank & 0x7F;
    if (bank == 0) bank = 1;
    return bank * 0x4000;
}

u32 MBC3::GetRAMBankOffset() const {
    return (m_ram_bank & 0x03) * 0x2000;
}

u8 MBC3::Read(u16 address) const {
    if (address <= 0x3FFF) {
        // ROM Bank 0
        return m_rom[address];
    } else if (address <= 0x7FFF) {
        // ROM Bank 1-127 (switchable)
        u32 offset = GetROMBankOffset() + (address - 0x4000);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
    }
    return 0xFF;
}

void MBC3::Write(u16 address, u8 value) {
    if (address <= 0x1FFF) {
        // RAM and Timer Enable
        m_ram_enabled = (value & 0x0F) == 0x0A;
    } else if (address <= 0x3FFF) {
        // ROM Bank Number (7 bits)
        m_rom_bank = value & 0x7F;
        if (m_rom_bank == 0) m_rom_bank = 1;
    } else if (address <= 0x5FFF) {
        // RAM Bank Number or RTC Register Select
        m_ram_bank = value;
    } else if (address <= 0x7FFF) {
        // Latch Clock Data
        if (m_rtc_latch_data == 0x00 && value == 0x01) {
            m_rtc_latched = true;
            // TODO: Latch current RTC values
        }
        m_rtc_latch_data = value;
    }
}

u8 MBC3::ReadRAM(u16 address) const {
    if (!m_ram_enabled) return 0xFF;

    if (m_ram_bank <= 0x03) {
        // RAM access
        u32 offset = GetRAMBankOffset() + (address - 0xA000);
        if (offset < m_ram.size()) {
            return m_ram[offset];
        }
    } else if (m_has_rtc && m_ram_bank >= 0x08 && m_ram_bank <= 0x0C) {
        // RTC register access
        switch (m_ram_bank) {
            case 0x08: return m_rtc_seconds;
            case 0x09: return m_rtc_minutes;
            case 0x0A: return m_rtc_hours;
            case 0x0B: return m_rtc_days_low;
            case 0x0C: return m_rtc_days_high;
        }
    }
    return 0xFF;
}

void MBC3::WriteRAM(u16 address, u8 value) {
    if (!m_ram_enabled) return;

    if (m_ram_bank <= 0x03) {
        // RAM access
        u32 offset = GetRAMBankOffset() + (address - 0xA000);
        if (offset < m_ram.size()) {
            m_ram[offset] = value;
        }
    } else if (m_has_rtc && m_ram_bank >= 0x08 && m_ram_bank <= 0x0C) {
        // RTC register write
        switch (m_ram_bank) {
            case 0x08: m_rtc_seconds = value; break;
            case 0x09: m_rtc_minutes = value; break;
            case 0x0A: m_rtc_hours = value; break;
            case 0x0B: m_rtc_days_low = value; break;
            case 0x0C: m_rtc_days_high = value; break;
        }
    }
}

bool MBC3::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());
    // TODO: Save RTC data
    return file.good();
}

bool MBC3::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    // TODO: Load RTC data
    return file.good();
}

// ============================================================================
// MBC5 Implementation
// ============================================================================

MBC5::MBC5(const u8* rom_data, size_t rom_size) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);

    // Allocate RAM per the cartridge header so .sav files match the cart
    m_ram.resize(RAMSizeFromHeader(rom_data, rom_size), 0);

    spdlog::info("MBC5 initialized with ROM size: {} bytes, RAM: {} bytes",
                 rom_size, m_ram.size());
}

u32 MBC5::GetROMBankOffset() const {
    return m_rom_bank * 0x4000;
}

u32 MBC5::GetRAMBankOffset() const {
    return (m_ram_bank & 0x0F) * 0x2000;
}

u8 MBC5::Read(u16 address) const {
    if (address <= 0x3FFF) {
        // ROM Bank 0
        return m_rom[address];
    } else if (address <= 0x7FFF) {
        // ROM Bank 0-511 (switchable)
        u32 offset = GetROMBankOffset() + (address - 0x4000);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
    }
    return 0xFF;
}

void MBC5::Write(u16 address, u8 value) {
    if (address <= 0x1FFF) {
        // RAM Enable
        m_ram_enabled = (value & 0x0F) == 0x0A;
    } else if (address <= 0x2FFF) {
        // ROM Bank Number (lower 8 bits)
        m_rom_bank = (m_rom_bank & 0x100) | value;
    } else if (address <= 0x3FFF) {
        // ROM Bank Number (9th bit)
        m_rom_bank = (m_rom_bank & 0x0FF) | ((value & 0x01) << 8);
    } else if (address <= 0x5FFF) {
        // RAM Bank Number (4 bits)
        m_ram_bank = value & 0x0F;
    }
}

u8 MBC5::ReadRAM(u16 address) const {
    if (!m_ram_enabled) return 0xFF;

    u32 offset = GetRAMBankOffset() + (address - 0xA000);
    if (offset < m_ram.size()) {
        return m_ram[offset];
    }
    return 0xFF;
}

void MBC5::WriteRAM(u16 address, u8 value) {
    if (!m_ram_enabled) return;

    u32 offset = GetRAMBankOffset() + (address - 0xA000);
    if (offset < m_ram.size()) {
        m_ram[offset] = value;
    }
}

bool MBC5::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());
    return file.good();
}

bool MBC5::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    return file.good();
}

// ============================================================================
// Game Genie ROM patches
// ============================================================================

std::vector<std::pair<u32, u8>> MBC::ApplyROMPatch(u16 address, u8 value,
                                                   bool has_compare, u8 compare) {
    std::vector<std::pair<u32, u8>> saved;

    if (address < 0x4000) {
        // Fixed bank: exactly one ROM location
        if (address < m_rom.size() &&
            (!has_compare || m_rom[address] == compare)) {
            saved.emplace_back(address, m_rom[address]);
            m_rom[address] = value;
        }
    } else if (address < 0x8000) {
        // Banked region: the CPU address maps to the same offset in every
        // bank. A compare byte tells us which banks the code is meant for;
        // without one, patching every bank would corrupt unrelated data,
        // so only bank 1 (the whole ROM for unbanked 32KB carts) is patched.
        u32 bank_offset = address - 0x4000;
        size_t max_bank = has_compare ? (m_rom.size() / 0x4000) : 2;
        for (size_t bank = 1; bank < max_bank; bank++) {
            u32 offset = static_cast<u32>(bank * 0x4000 + bank_offset);
            if (offset < m_rom.size() &&
                (!has_compare || m_rom[offset] == compare)) {
                saved.emplace_back(offset, m_rom[offset]);
                m_rom[offset] = value;
            }
        }
    }

    return saved;
}

void MBC::RestoreROM(const std::vector<std::pair<u32, u8>>& saved) {
    for (const auto& [offset, original] : saved) {
        if (offset < m_rom.size()) {
            m_rom[offset] = original;
        }
    }
}

// ============================================================================
// Save states
// ============================================================================

void MBC::SaveState(StateBuffer& state) const {
    state.Write(m_ram_enabled);
    state.WriteBytes(m_ram.data(), m_ram.size());
}

bool MBC::LoadState(StateBuffer& state) {
    return state.Read(m_ram_enabled) &&
           state.ReadBytes(m_ram.data(), m_ram.size());
}

void MBC1::SaveState(StateBuffer& state) const {
    MBC::SaveState(state);
    state.Write(m_rom_bank);
    state.Write(m_ram_bank);
    state.Write(m_banking_mode);
}

bool MBC1::LoadState(StateBuffer& state) {
    return MBC::LoadState(state) &&
           state.Read(m_rom_bank) &&
           state.Read(m_ram_bank) &&
           state.Read(m_banking_mode);
}

void MBC3::SaveState(StateBuffer& state) const {
    MBC::SaveState(state);
    state.Write(m_rom_bank);
    state.Write(m_ram_bank);
    state.Write(m_rtc_seconds);
    state.Write(m_rtc_minutes);
    state.Write(m_rtc_hours);
    state.Write(m_rtc_days_low);
    state.Write(m_rtc_days_high);
    state.Write(m_rtc_latch_data);
    state.Write(m_rtc_latched);
}

bool MBC3::LoadState(StateBuffer& state) {
    return MBC::LoadState(state) &&
           state.Read(m_rom_bank) &&
           state.Read(m_ram_bank) &&
           state.Read(m_rtc_seconds) &&
           state.Read(m_rtc_minutes) &&
           state.Read(m_rtc_hours) &&
           state.Read(m_rtc_days_low) &&
           state.Read(m_rtc_days_high) &&
           state.Read(m_rtc_latch_data) &&
           state.Read(m_rtc_latched);
}

void MBC5::SaveState(StateBuffer& state) const {
    MBC::SaveState(state);
    state.Write(m_rom_bank);
    state.Write(m_ram_bank);
}

bool MBC5::LoadState(StateBuffer& state) {
    return MBC::LoadState(state) &&
           state.Read(m_rom_bank) &&
           state.Read(m_ram_bank);
}
