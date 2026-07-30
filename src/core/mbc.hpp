#pragma once
#include "types.hpp"
#include <vector>
#include <memory>
#include <string>
#include <utility>

class StateBuffer;

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

    // Save states: base serializes RAM contents and the enable flag;
    // subclasses append their banking registers
    virtual void SaveState(StateBuffer& state) const;
    virtual bool LoadState(StateBuffer& state);

    // Game Genie support: patch every ROM location that maps to the given
    // CPU address (single location in the fixed bank; all banks for
    // 0x4000-0x7FFF when a compare byte narrows it down, otherwise just
    // bank 1). Returns the patched offsets with their original bytes so
    // the patch can be undone when the cheat is disabled.
    std::vector<std::pair<u32, u8>> ApplyROMPatch(u16 address, u8 value,
                                                  bool has_compare, u8 compare);
    void RestoreROM(const std::vector<std::pair<u32, u8>>& saved);

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
    void SaveState(StateBuffer& state) const override;
    bool LoadState(StateBuffer& state) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (1-127)
    u8 m_ram_bank = 0;      // RAM bank number (0-3)
    bool m_banking_mode = false;  // false = ROM banking, true = RAM banking

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;
};

// MBC2 - Up to 256KB ROM, built-in 512x4-bit RAM
class MBC2 : public MBC {
public:
    MBC2(const u8* rom_data, size_t rom_size);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;
    void SaveState(StateBuffer& state) const override;
    bool LoadState(StateBuffer& state) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (1-15)
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
    void SaveState(StateBuffer& state) const override;
    bool LoadState(StateBuffer& state) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (1-127)
    u8 m_ram_bank = 0;      // RAM bank or RTC register select (0-3 = RAM, 8-12 = RTC)
    bool m_has_rtc;

    // Live RTC counters, valid as of m_rtc_timestamp (unix seconds).
    // Elapsed wall-clock time is folded in by UpdateRTC before any access.
    u8 m_rtc_seconds = 0;
    u8 m_rtc_minutes = 0;
    u8 m_rtc_hours = 0;
    u8 m_rtc_days_low = 0;
    u8 m_rtc_days_high = 0;   // Bit 0: day bit 8, bit 6: halt, bit 7: day carry
    s64 m_rtc_timestamp = 0;

    // Latched copies: reads return these, frozen by the 0x00->0x01 latch
    u8 m_latched_seconds = 0;
    u8 m_latched_minutes = 0;
    u8 m_latched_hours = 0;
    u8 m_latched_days_low = 0;
    u8 m_latched_days_high = 0;
    u8 m_rtc_latch_data = 0;

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;

    // Advance the live counters by elapsed wall-clock time
    void UpdateRTC();
    void LatchRTC();
};

// HuC1 - MBC1-like banking plus an infrared port (stubbed: no receiver)
class HuC1 : public MBC {
public:
    HuC1(const u8* rom_data, size_t rom_size);

    u8 Read(u16 address) const override;
    void Write(u16 address, u8 value) override;
    u8 ReadRAM(u16 address) const override;
    void WriteRAM(u16 address, u8 value) override;
    bool SaveRAM(const std::string& path) override;
    bool LoadRAM(const std::string& path) override;
    void SaveState(StateBuffer& state) const override;
    bool LoadState(StateBuffer& state) override;

private:
    u8 m_rom_bank = 1;      // ROM bank number (6 bits)
    u8 m_ram_bank = 0;      // RAM bank number (0-3)
    bool m_ir_mode = false; // 0xA000-0xBFFF maps IR instead of RAM
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
    void SaveState(StateBuffer& state) const override;
    bool LoadState(StateBuffer& state) override;

private:
    u16 m_rom_bank = 1;     // ROM bank number (0-511)
    u8 m_ram_bank = 0;      // RAM bank number (0-15)

    u32 GetROMBankOffset() const;
    u32 GetRAMBankOffset() const;
};
