#pragma once
#include "../types.hpp"
#include <vector>
#include <memory>
#include <string>

// Base class for Memory Bank Controllers
class MBC {
public:
    virtual ~MBC() = default;

    // ROM read/write (write is for banking control)
    virtual u8 Read(u16 address) const = 0;
    virtual void Write(u16 address, u8 value) = 0;

    // External RAM read/write
    virtual u8 ReadRAM(u16 address) const = 0;
    virtual void WriteRAM(u16 address, u8 value) = 0;

    // Save/Load external RAM
    virtual bool SaveRAM(const std::string& path) = 0;
    virtual bool LoadRAM(const std::string& path) = 0;

    // Factory method to create appropriate MBC based on cartridge type
    static std::unique_ptr<MBC> Create(u8 cartridge_type, const u8* rom_data, size_t rom_size);

protected:
    std::vector<u8> m_rom;
    std::vector<u8> m_ram;
    bool m_ram_enabled = false;
};

// MBC0 - No MBC (32KB ROM only, no banking)
class MBC0 : public MBC {
public:
    MBC0(const u8* rom_data, size_t rom_size);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;
};

// MBC1 - Up to 2MB ROM, 32KB RAM
class MBC1 : public MBC {
public:
    MBC1(const u8* rom_data, size_t rom_size);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (1-127)
    u8 m_ram_bank = 0;      // RAM bank number (0-3)
    bool m_banking_mode = false;  // false = ROM banking, true = RAM banking

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;
};

// MBC3 - Up to 2MB ROM, 32KB RAM, RTC (Real-Time Clock)
class MBC3 : public MBC {
public:
    MBC3(const u8* rom_data, size_t rom_size, bool has_rtc);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (1-127)
    u8 m_ram_bank = 0;      // RAM bank or RTC register select (0-3 = RAM, 8-12 = RTC)
    bool m_has_rtc;

    // RTC registers
    u8 m_rtc_seconds = 0;
    u8 m_rtc_minutes = 0;
    u8 m_rtc_hours = 0;
    u8 m_rtc_days_low = 0;
    u8 m_rtc_days_high = 0;

    // RTC latch
    u8 m_rtc_latch_data = 0;
    bool m_rtc_latched = false;

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;
};

// MBC5 - Up to 8MB ROM, 128KB RAM
class MBC5 : public MBC {
public:
    MBC5(const u8* rom_data, size_t rom_size);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;

private:
    u16 m_rom_bank = 1;     // ROM bank number (0-511)
    u8 m_ram_bank = 0;      // RAM bank number (0-15)

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;
};
