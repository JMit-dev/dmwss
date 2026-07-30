#include "mbc.hpp"
#include "../machine/savestate.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstring>
#include <ctime>

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

        case 0x05:  // MBC2
        case 0x06:  // MBC2+BATTERY
            return std::make_unique<MBC2>(rom_data, rom_size);

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

        case 0xFF:  // HuC1+RAM+BATTERY
            return std::make_unique<HuC1>(rom_data, rom_size);

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
// MBC2 Implementation (built-in 512x4-bit RAM)
// ============================================================================

MBC2::MBC2(const u8* rom_data, size_t rom_size) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);

    // The RAM is on the MBC chip itself: 512 half-byte cells
    m_ram.resize(512, 0);

    spdlog::info("MBC2 initialized with ROM size: {} bytes", rom_size);
}

u8 MBC2::Read(u16 address) const {
    if (address <= 0x3FFF) {
        // ROM Bank 0
        return m_rom[address];
    } else if (address <= 0x7FFF) {
        u8 bank = m_rom_bank & 0x0F;
        if (bank == 0) bank = 1;
        u32 offset = bank * 0x4000 + (address - 0x4000);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
    }
    return 0xFF;
}

void MBC2::Write(u16 address, u8 value) {
    if (address <= 0x3FFF) {
        // Address bit 8 selects the function: clear = RAM enable,
        // set = ROM bank select (there is only one control range)
        if (address & 0x0100) {
            m_rom_bank = value & 0x0F;
            if (m_rom_bank == 0) m_rom_bank = 1;
        } else {
            m_ram_enabled = (value & 0x0F) == 0x0A;
        }
    }
}

u8 MBC2::ReadRAM(u16 address) const {
    if (!m_ram_enabled) return 0xFF;

    // Only 512 cells exist; the region echoes through 0xA000-0xBFFF.
    // Cells are 4 bits wide - the upper nibble reads open-bus (1s).
    return m_ram[address & 0x1FF] | 0xF0;
}

void MBC2::WriteRAM(u16 address, u8 value) {
    if (!m_ram_enabled) return;
    m_ram[address & 0x1FF] = value & 0x0F;
}

bool MBC2::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());
    return file.good();
}

bool MBC2::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    return file.good();
}

void MBC2::SaveState(StateBuffer& state) const {
    MBC::SaveState(state);
    state.Write(m_rom_bank);
}

bool MBC2::LoadState(StateBuffer& state) {
    return MBC::LoadState(state) &&
           state.Read(m_rom_bank);
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

    m_rtc_timestamp = static_cast<s64>(std::time(nullptr));

    spdlog::info("MBC3 initialized with ROM size: {} bytes, RAM: {} bytes, RTC: {}",
                 rom_size, m_ram.size(), has_rtc);
}

void MBC3::UpdateRTC() {
    s64 now = static_cast<s64>(std::time(nullptr));

    // Halted: the counters freeze, but keep the timestamp current so
    // time spent halted is not folded in when the clock resumes
    if (m_rtc_days_high & 0x40) {
        m_rtc_timestamp = now;
        return;
    }

    s64 elapsed = now - m_rtc_timestamp;
    if (elapsed <= 0) return;
    m_rtc_timestamp = now;

    s64 seconds = (m_rtc_seconds & 0x3F) + elapsed;
    s64 minutes = (m_rtc_minutes & 0x3F) + seconds / 60;
    s64 hours = (m_rtc_hours & 0x1F) + minutes / 60;
    s64 days = (m_rtc_days_low | ((m_rtc_days_high & 0x01) << 8)) + hours / 24;

    m_rtc_seconds = static_cast<u8>(seconds % 60);
    m_rtc_minutes = static_cast<u8>(minutes % 60);
    m_rtc_hours = static_cast<u8>(hours % 24);
    m_rtc_days_low = static_cast<u8>(days & 0xFF);
    m_rtc_days_high = (m_rtc_days_high & 0xFE) | ((days >> 8) & 0x01);
    if (days > 511) {
        m_rtc_days_high |= 0x80;  // Day counter carry, sticky until cleared
    }
}

void MBC3::LatchRTC() {
    UpdateRTC();
    m_latched_seconds = m_rtc_seconds;
    m_latched_minutes = m_rtc_minutes;
    m_latched_hours = m_rtc_hours;
    m_latched_days_low = m_rtc_days_low;
    m_latched_days_high = m_rtc_days_high;
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
        // Latch Clock Data: a 0x00 -> 0x01 write freezes the readable copy
        if (m_rtc_latch_data == 0x00 && value == 0x01 && m_has_rtc) {
            LatchRTC();
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
        // RTC register access: reads see the latched copy
        switch (m_ram_bank) {
            case 0x08: return m_latched_seconds & 0x3F;
            case 0x09: return m_latched_minutes & 0x3F;
            case 0x0A: return m_latched_hours & 0x1F;
            case 0x0B: return m_latched_days_low;
            case 0x0C: return m_latched_days_high & 0xC1;
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
        // RTC register write: fold in elapsed time first so setting one
        // register doesn't discard time accrued on the others. Writes hit
        // the live counters and the latched copy.
        UpdateRTC();
        switch (m_ram_bank) {
            case 0x08:
                m_rtc_seconds = value & 0x3F;
                m_latched_seconds = m_rtc_seconds;
                // Writing seconds also resets the sub-second counter
                m_rtc_timestamp = static_cast<s64>(std::time(nullptr));
                break;
            case 0x09:
                m_rtc_minutes = value & 0x3F;
                m_latched_minutes = m_rtc_minutes;
                break;
            case 0x0A:
                m_rtc_hours = value & 0x1F;
                m_latched_hours = m_rtc_hours;
                break;
            case 0x0B:
                m_rtc_days_low = value;
                m_latched_days_low = value;
                break;
            case 0x0C:
                m_rtc_days_high = value & 0xC1;
                m_latched_days_high = m_rtc_days_high;
                break;
        }
    }
}

bool MBC3::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());

    // RTC carts append the 48-byte footer other emulators (VBA, BGB,
    // SameBoy) use: live regs, latched regs (u32 LE each), unix timestamp
    if (m_has_rtc) {
        UpdateRTC();
        auto write_u32 = [&file](u32 value) {
            u8 bytes[4] = {static_cast<u8>(value), static_cast<u8>(value >> 8),
                           static_cast<u8>(value >> 16), static_cast<u8>(value >> 24)};
            file.write(reinterpret_cast<const char*>(bytes), 4);
        };
        write_u32(m_rtc_seconds);
        write_u32(m_rtc_minutes);
        write_u32(m_rtc_hours);
        write_u32(m_rtc_days_low);
        write_u32(m_rtc_days_high);
        write_u32(m_latched_seconds);
        write_u32(m_latched_minutes);
        write_u32(m_latched_hours);
        write_u32(m_latched_days_low);
        write_u32(m_latched_days_high);
        u64 stamp = static_cast<u64>(m_rtc_timestamp);
        write_u32(static_cast<u32>(stamp));
        write_u32(static_cast<u32>(stamp >> 32));
    }
    return file.good();
}

bool MBC3::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    if (!file.good()) return false;

    if (m_has_rtc) {
        auto read_u32 = [&file](u32& value) {
            u8 bytes[4];
            file.read(reinterpret_cast<char*>(bytes), 4);
            value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
                    (static_cast<u32>(bytes[3]) << 24);
        };
        u32 regs[10];
        for (u32& reg : regs) read_u32(reg);
        u32 stamp_low = 0;
        u32 stamp_high = 0;
        read_u32(stamp_low);
        read_u32(stamp_high);

        // Old .sav files without the footer keep the fresh power-on clock
        if (file.good()) {
            m_rtc_seconds = static_cast<u8>(regs[0]);
            m_rtc_minutes = static_cast<u8>(regs[1]);
            m_rtc_hours = static_cast<u8>(regs[2]);
            m_rtc_days_low = static_cast<u8>(regs[3]);
            m_rtc_days_high = static_cast<u8>(regs[4]);
            m_latched_seconds = static_cast<u8>(regs[5]);
            m_latched_minutes = static_cast<u8>(regs[6]);
            m_latched_hours = static_cast<u8>(regs[7]);
            m_latched_days_low = static_cast<u8>(regs[8]);
            m_latched_days_high = static_cast<u8>(regs[9]);
            m_rtc_timestamp = static_cast<s64>(
                stamp_low | (static_cast<u64>(stamp_high) << 32));
            // Fold in the wall-clock time that passed since the save
            UpdateRTC();
        }
    }
    return true;
}

// ============================================================================
// HuC1 Implementation (MBC1-like with an infrared port)
// ============================================================================

HuC1::HuC1(const u8* rom_data, size_t rom_size) {
    m_rom.resize(rom_size);
    std::memcpy(m_rom.data(), rom_data, rom_size);

    // Allocate RAM per the cartridge header so .sav files match the cart
    m_ram.resize(RAMSizeFromHeader(rom_data, rom_size), 0);

    // HuC1 has no RAM-enable gate; the 0x0000 range selects RAM vs IR
    m_ram_enabled = true;

    spdlog::info("HuC1 initialized with ROM size: {} bytes, RAM: {} bytes",
                 rom_size, m_ram.size());
}

u8 HuC1::Read(u16 address) const {
    if (address <= 0x3FFF) {
        return m_rom[address];
    } else if (address <= 0x7FFF) {
        u32 offset = (m_rom_bank & 0x3F) * 0x4000 + (address - 0x4000);
        if (offset < m_rom.size()) {
            return m_rom[offset];
        }
    }
    return 0xFF;
}

void HuC1::Write(u16 address, u8 value) {
    if (address <= 0x1FFF) {
        // 0x0E maps the IR register into 0xA000-0xBFFF, anything else RAM
        m_ir_mode = (value & 0x0F) == 0x0E;
    } else if (address <= 0x3FFF) {
        // ROM bank, no bank-0 remapping on HuC1
        m_rom_bank = value & 0x3F;
    } else if (address <= 0x5FFF) {
        m_ram_bank = value & 0x03;
    }
}

u8 HuC1::ReadRAM(u16 address) const {
    if (m_ir_mode) {
        return 0xC0;  // No IR light seen
    }
    u32 offset = (m_ram_bank & 0x03) * 0x2000 + (address - 0xA000);
    if (offset < m_ram.size()) {
        return m_ram[offset];
    }
    return 0xFF;
}

void HuC1::WriteRAM(u16 address, u8 value) {
    if (m_ir_mode) {
        return;  // IR LED writes are dropped
    }
    u32 offset = (m_ram_bank & 0x03) * 0x2000 + (address - 0xA000);
    if (offset < m_ram.size()) {
        m_ram[offset] = value;
    }
}

bool HuC1::SaveRAM(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(m_ram.data()), m_ram.size());
    return file.good();
}

bool HuC1::LoadRAM(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(m_ram.data()), m_ram.size());
    return file.good();
}

void HuC1::SaveState(StateBuffer& state) const {
    MBC::SaveState(state);
    state.Write(m_rom_bank);
    state.Write(m_ram_bank);
    state.Write(m_ir_mode);
}

bool HuC1::LoadState(StateBuffer& state) {
    return MBC::LoadState(state) &&
           state.Read(m_rom_bank) &&
           state.Read(m_ram_bank) &&
           state.Read(m_ir_mode);
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
    state.Write(m_rtc_timestamp);
    state.Write(m_latched_seconds);
    state.Write(m_latched_minutes);
    state.Write(m_latched_hours);
    state.Write(m_latched_days_low);
    state.Write(m_latched_days_high);
    state.Write(m_rtc_latch_data);
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
           state.Read(m_rtc_timestamp) &&
           state.Read(m_latched_seconds) &&
           state.Read(m_latched_minutes) &&
           state.Read(m_latched_hours) &&
           state.Read(m_latched_days_low) &&
           state.Read(m_latched_days_high) &&
           state.Read(m_rtc_latch_data);
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
