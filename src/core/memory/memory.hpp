#pragma once
#include "../types.hpp"
#include <array>
#include <functional>
#include <memory>

// Forward declarations
class MBC;

class Memory {
public:
    // Game Boy memory map constants
    static constexpr u16 ROM_BANK_0_START = 0x0000;
    static constexpr u16 ROM_BANK_0_END   = 0x3FFF;
    static constexpr u16 ROM_BANK_N_START = 0x4000;
    static constexpr u16 ROM_BANK_N_END   = 0x7FFF;
    static constexpr u16 VRAM_START       = 0x8000;
    static constexpr u16 VRAM_END         = 0x9FFF;
    static constexpr u16 EXTERNAL_RAM_START = 0xA000;
    static constexpr u16 EXTERNAL_RAM_END   = 0xBFFF;
    static constexpr u16 WRAM_START       = 0xC000;
    static constexpr u16 WRAM_END         = 0xDFFF;
    static constexpr u16 ECHO_RAM_START   = 0xE000;
    static constexpr u16 ECHO_RAM_END     = 0xFDFF;
    static constexpr u16 OAM_START        = 0xFE00;
    static constexpr u16 OAM_END          = 0xFE9F;
    static constexpr u16 UNUSABLE_START   = 0xFEA0;
    static constexpr u16 UNUSABLE_END     = 0xFEFF;
    static constexpr u16 IO_START         = 0xFF00;
    static constexpr u16 IO_END           = 0xFF7F;
    static constexpr u16 HRAM_START       = 0xFF80;
    static constexpr u16 HRAM_END         = 0xFFFE;
    static constexpr u16 IE_REGISTER      = 0xFFFF;

    // Memory sizes
    static constexpr size_t WRAM_SIZE  = 8192;   // 8KB
    static constexpr size_t VRAM_SIZE  = 8192;   // 8KB
    static constexpr size_t OAM_SIZE   = 160;    // 160 bytes
    static constexpr size_t HRAM_SIZE  = 127;    // 127 bytes
    static constexpr size_t IO_SIZE    = 128;    // 128 bytes

    // Software fastmem page table configuration
    static constexpr size_t PAGE_SIZE = 256;     // 256 bytes per page
    static constexpr size_t PAGE_COUNT = 256;    // 64KB / 256 = 256 pages

    Memory();
    ~Memory();

    // Read/Write operations (these use fastmem)
    u8 Read(u16 address) const;
    void Write(u16 address, u8 value);

    // 16-bit read/write helpers (little-endian)
    u16 Read16(u16 address) const;
    void Write16(u16 address, u16 value);

    // Load ROM data
    bool LoadROM(const u8* data, size_t size);

    // Direct memory access (for debugging/testing)
    u8* GetWRAM() { return m_wram.data(); }
    u8* GetVRAM() { return m_vram.data(); }
    u8* GetOAM() { return m_oam.data(); }
    u8* GetHRAM() { return m_hram.data(); }

    // Reset memory
    void Reset();

    // I/O register handler types
    using IOReadHandler = std::function<u8(u16)>;
    using IOWriteHandler = std::function<void(u16, u8)>;

    // Register I/O handlers (public so PPU/APU can register)
    void RegisterIOHandler(u16 address, IOReadHandler read_handler, IOWriteHandler write_handler);

private:
    // Memory regions (SIMD-aligned for performance)
    ALIGN(64) std::array<u8, WRAM_SIZE> m_wram;   // Work RAM
    ALIGN(64) std::array<u8, VRAM_SIZE> m_vram;   // Video RAM
    ALIGN(64) std::array<u8, OAM_SIZE> m_oam;     // OAM (Sprite attribute table)
    ALIGN(64) std::array<u8, HRAM_SIZE> m_hram;   // High RAM
    ALIGN(64) std::array<u8, IO_SIZE> m_io;       // I/O registers

    u8 m_ie_register;  // Interrupt Enable register (0xFFFF)

    // Software fastmem page tables
    // Each entry points to the start of a page, or nullptr for I/O regions
    std::array<u8*, PAGE_COUNT> m_read_page_table;
    std::array<u8*, PAGE_COUNT> m_write_page_table;

    // Memory Bank Controller (for ROM banking)
    std::unique_ptr<MBC> m_mbc;

    // Initialize page tables
    void InitializePageTables();

    // Slow path handlers for I/O and unmapped regions
    u8 ReadIO(u16 address) const;
    void WriteIO(u16 address, u8 value);

    // I/O register handler storage
    std::array<IOReadHandler, IO_SIZE> m_io_read_handlers;
    std::array<IOWriteHandler, IO_SIZE> m_io_write_handlers;
};
