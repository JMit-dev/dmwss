#include "memory.hpp"
#include "mbc.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

Memory::Memory()
    : m_ie_register(0)
    , m_mbc(nullptr) {
    Reset();
}

Memory::~Memory() = default;

void Memory::Reset() {
    // Zero out all memory regions
    m_wram.fill(0);
    m_vram.fill(0);
    m_oam.fill(0);
    m_hram.fill(0);
    m_io.fill(0);
    m_ie_register = 0;

    // Initialize page tables
    InitializePageTables();

    spdlog::debug("Memory system reset");
}

void Memory::InitializePageTables() {
    // Initialize all pages to nullptr (slow path)
    m_read_page_table.fill(nullptr);
    m_write_page_table.fill(nullptr);

    // Map VRAM (0x8000-0x9FFF) - 32 pages (8KB / 256 bytes)
    for (size_t i = 0; i < 32; i++) {
        u16 page_index = (VRAM_START + i * PAGE_SIZE) / PAGE_SIZE;
        m_read_page_table[page_index] = m_vram.data() + (i * PAGE_SIZE);
        m_write_page_table[page_index] = m_vram.data() + (i * PAGE_SIZE);
    }

    // Map WRAM (0xC000-0xDFFF) - 32 pages (8KB / 256 bytes)
    for (size_t i = 0; i < 32; i++) {
        u16 page_index = (WRAM_START + i * PAGE_SIZE) / PAGE_SIZE;
        m_read_page_table[page_index] = m_wram.data() + (i * PAGE_SIZE);
        m_write_page_table[page_index] = m_wram.data() + (i * PAGE_SIZE);
    }

    // Map Echo RAM (0xE000-0xFDFF) - mirrors WRAM
    // Echo RAM is WRAM - 0x2000, so we map it to the same physical pages
    for (size_t i = 0; i < 30; i++) {  // 0xFE00 - 0xE000 = 0x1E00 = 7680 bytes = 30 pages
        u16 page_index = (ECHO_RAM_START + i * PAGE_SIZE) / PAGE_SIZE;
        size_t wram_offset = i * PAGE_SIZE;
        if (wram_offset < WRAM_SIZE) {
            m_read_page_table[page_index] = m_wram.data() + wram_offset;
            m_write_page_table[page_index] = m_wram.data() + wram_offset;
        }
    }

    // HRAM (0xFF80-0xFFFE) is in the I/O page, handled by slow path
    // OAM, I/O, and ROM are also handled by slow path

    spdlog::trace("Page tables initialized");
}

u8 Memory::Read(u16 address) const {
    const u8 page = address / PAGE_SIZE;
    const u8 offset = address % PAGE_SIZE;
    u8* page_ptr = m_read_page_table[page];

    if (LIKELY(page_ptr != nullptr)) {
        // Fast path: direct memory access
        return page_ptr[offset];
    }

    // Slow path: handle special regions
    if (address >= ROM_BANK_0_START && address <= ROM_BANK_N_END) {
        // ROM access - delegate to MBC
        if (m_mbc) {
            return m_mbc->Read(address);
        }
        spdlog::warn("Read from ROM address 0x{:04X} but no ROM loaded", address);
        return 0xFF;
    }
    else if (address >= EXTERNAL_RAM_START && address <= EXTERNAL_RAM_END) {
        // External RAM - delegate to MBC
        if (m_mbc) {
            return m_mbc->ReadRAM(address);
        }
        return 0xFF;
    }
    else if (address >= OAM_START && address <= OAM_END) {
        // OAM
        return m_oam[address - OAM_START];
    }
    else if (address >= UNUSABLE_START && address <= UNUSABLE_END) {
        // Unusable memory region
        return 0xFF;
    }
    else if (address >= IO_START && address <= IO_END) {
        // I/O registers
        return ReadIO(address);
    }
    else if (address >= HRAM_START && address <= HRAM_END) {
        // High RAM
        return m_hram[address - HRAM_START];
    }
    else if (address == IE_REGISTER) {
        // Interrupt Enable register
        return m_ie_register;
    }

    spdlog::warn("Read from unmapped address 0x{:04X}", address);
    return 0xFF;
}

void Memory::Write(u16 address, u8 value) {
    const u8 page = address / PAGE_SIZE;
    const u8 offset = address % PAGE_SIZE;
    u8* page_ptr = m_write_page_table[page];

    if (LIKELY(page_ptr != nullptr)) {
        // Fast path: direct memory access
        page_ptr[offset] = value;
        return;
    }

    // Slow path: handle special regions
    if (address >= ROM_BANK_0_START && address <= ROM_BANK_N_END) {
        // ROM write - delegate to MBC (for banking control)
        if (m_mbc) {
            m_mbc->Write(address, value);
        }
        return;
    }
    else if (address >= EXTERNAL_RAM_START && address <= EXTERNAL_RAM_END) {
        // External RAM - delegate to MBC
        if (m_mbc) {
            m_mbc->WriteRAM(address, value);
        }
        return;
    }
    else if (address >= OAM_START && address <= OAM_END) {
        // OAM
        m_oam[address - OAM_START] = value;
        return;
    }
    else if (address >= UNUSABLE_START && address <= UNUSABLE_END) {
        // Unusable memory region - ignore writes
        return;
    }
    else if (address >= IO_START && address <= IO_END) {
        // I/O registers
        WriteIO(address, value);
        return;
    }
    else if (address >= HRAM_START && address <= HRAM_END) {
        // High RAM
        m_hram[address - HRAM_START] = value;
        return;
    }
    else if (address == IE_REGISTER) {
        // Interrupt Enable register
        m_ie_register = value;
        return;
    }

    spdlog::warn("Write to unmapped address 0x{:04X} = 0x{:02X}", address, value);
}

u16 Memory::Read16(u16 address) const {
    // Little-endian: low byte first, then high byte
    u8 low = Read(address);
    u8 high = Read(address + 1);
    return (static_cast<u16>(high) << 8) | low;
}

void Memory::Write16(u16 address, u16 value) {
    // Little-endian: low byte first, then high byte
    Write(address, static_cast<u8>(value & 0xFF));
    Write(address + 1, static_cast<u8>((value >> 8) & 0xFF));
}

bool Memory::LoadROM(const u8* data, size_t size) {
    if (!data || size < 0x150) {
        spdlog::error("Invalid ROM data or size too small");
        return false;
    }

    // Read cartridge header to determine MBC type
    u8 cartridge_type = data[0x0147];

    // Create appropriate MBC
    m_mbc = MBC::Create(cartridge_type, data, size);
    if (!m_mbc) {
        spdlog::error("Failed to create MBC for cartridge type 0x{:02X}", cartridge_type);
        return false;
    }

    spdlog::info("ROM loaded successfully, cartridge type: 0x{:02X}, size: {} bytes",
                 cartridge_type, size);
    return true;
}

u8 Memory::ReadIO(u16 address) const {
    // Default I/O register behavior: read from buffer
    u16 offset = address - IO_START;

    // Check if we have a custom handler
    if (m_io_read_handlers[offset]) {
        return m_io_read_handlers[offset](address);
    }

    return m_io[offset];
}

void Memory::WriteIO(u16 address, u8 value) {
    // Default I/O register behavior: write to buffer
    u16 offset = address - IO_START;

    // Check if we have a custom handler
    if (m_io_write_handlers[offset]) {
        m_io_write_handlers[offset](address, value);
    } else {
        m_io[offset] = value;
    }
}

void Memory::RegisterIOHandler(u16 address, IOReadHandler read_handler, IOWriteHandler write_handler) {
    if (address < IO_START || address > IO_END) {
        spdlog::warn("Attempted to register I/O handler for invalid address 0x{:04X}", address);
        return;
    }

    u16 offset = address - IO_START;
    m_io_read_handlers[offset] = std::move(read_handler);
    m_io_write_handlers[offset] = std::move(write_handler);

    spdlog::trace("Registered I/O handler for address 0x{:04X}", address);
}

void Memory::RequestInterrupt(u8 interrupt_bit) {
    // Directly set the bit in IF register (0xFF0F)
    // IF register is at offset 0x0F in the I/O region
    m_io[0x0F] |= interrupt_bit;
    spdlog::trace("Interrupt requested: bit 0x{:02X}, IF now 0x{:02X}", interrupt_bit, m_io[0x0F]);
}
