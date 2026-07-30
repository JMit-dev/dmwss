#include "memory.hpp"
#include "../machine/savestate.hpp"
#include "mbc.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

Memory::Memory()
    : m_ie_register(0)
    , m_mbc(nullptr) {
    Reset();
    RegisterBankingHandlers();
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
    m_vram_bank = 0;
    m_wram_bank = 1;

    // Initialize page tables
    InitializePageTables();

    spdlog::debug("Memory system reset");
}

void Memory::InitializePageTables() {
    // Initialize all pages to nullptr (slow path)
    m_read_page_table.fill(nullptr);
    m_write_page_table.fill(nullptr);

    // VRAM, WRAM and Echo RAM depend on the current VBK/SVBK banks
    UpdateBankedPageTables();

    // HRAM (0xFF80-0xFFFE) is in the I/O page, handled by slow path
    // OAM, I/O, and ROM are also handled by slow path

    spdlog::trace("Page tables initialized");
}

void Memory::UpdateBankedPageTables() {
    auto map = [this](u16 start, u8* target, size_t pages) {
        for (size_t i = 0; i < pages; i++) {
            u16 page_index = (start / PAGE_SIZE) + i;
            m_read_page_table[page_index] = target + (i * PAGE_SIZE);
            m_write_page_table[page_index] = target + (i * PAGE_SIZE);
        }
    };

    // VRAM (0x8000-0x9FFF): whole region switches with VBK
    map(VRAM_START, m_vram.data() + m_vram_bank * VRAM_BANK_SIZE, 32);

    // WRAM: 0xC000-0xCFFF is always bank 0, 0xD000-0xDFFF switches with SVBK
    u8* wram_hi = m_wram.data() + m_wram_bank * WRAM_BANK_SIZE;
    map(WRAM_START, m_wram.data(), 16);
    map(WRAM_START + 0x1000, wram_hi, 16);

    // Echo RAM (0xE000-0xFDFF) mirrors 0xC000-0xDDFF
    map(ECHO_RAM_START, m_wram.data(), 16);
    map(ECHO_RAM_START + 0x1000, wram_hi, 14);
}

void Memory::RegisterBankingHandlers() {
    // VBK - VRAM bank select (0xFF4F, CGB only)
    RegisterIOHandler(0xFF4F,
        [this](u16) -> u8 {
            return m_cgb_mode ? (0xFE | m_vram_bank) : 0xFF;
        },
        [this](u16, u8 value) {
            if (!m_cgb_mode) return;
            m_vram_bank = value & 0x01;
            UpdateBankedPageTables();
        }
    );

    // SVBK - WRAM bank select (0xFF70, CGB only); bank 0 selects bank 1
    RegisterIOHandler(0xFF70,
        [this](u16) -> u8 {
            return m_cgb_mode ? (0xF8 | m_wram_bank) : 0xFF;
        },
        [this](u16, u8 value) {
            if (!m_cgb_mode) return;
            m_wram_bank = value & 0x07;
            if (m_wram_bank == 0) m_wram_bank = 1;
            UpdateBankedPageTables();
        }
    );
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

    // Battery-backed cartridge types keep their RAM across power cycles
    switch (cartridge_type) {
        case 0x03:  // MBC1+RAM+BATTERY
        case 0x06:  // MBC2+BATTERY
        case 0x09:  // ROM+RAM+BATTERY
        case 0x0D:  // MMM01+RAM+BATTERY
        case 0x0F:  // MBC3+TIMER+BATTERY
        case 0x10:  // MBC3+TIMER+RAM+BATTERY
        case 0x13:  // MBC3+RAM+BATTERY
        case 0x1B:  // MBC5+RAM+BATTERY
        case 0x1E:  // MBC5+RUMBLE+RAM+BATTERY
        case 0xFF:  // HuC1+RAM+BATTERY
            m_has_battery = true;
            break;
        default:
            m_has_battery = false;
            break;
    }

    spdlog::info("ROM loaded successfully, cartridge type: 0x{:02X}, size: {} bytes",
                 cartridge_type, size);
    return true;
}

bool Memory::SaveCartRAM(const std::string& path) {
    return m_mbc ? m_mbc->SaveRAM(path) : false;
}

bool Memory::LoadCartRAM(const std::string& path) {
    return m_mbc ? m_mbc->LoadRAM(path) : false;
}

u8 Memory::ReadIO(u16 address) const {
    // Default I/O register behavior: read from buffer
    u16 offset = address - IO_START;

    // Check if we have a custom handler
    if (m_io_read_handlers[offset]) {
        return m_io_read_handlers[offset](address);
    }

    // IF (0xFF0F): upper 3 bits are unimplemented on hardware and read as 1
    if (address == 0xFF0F) {
        return m_io[offset] | 0xE0;
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

void Memory::SaveState(StateBuffer& state) const {
    state.WriteBytes(m_wram.data(), m_wram.size());
    state.WriteBytes(m_vram.data(), m_vram.size());
    state.WriteBytes(m_oam.data(), m_oam.size());
    state.WriteBytes(m_hram.data(), m_hram.size());
    state.WriteBytes(m_io.data(), m_io.size());
    state.Write(m_ie_register);
    state.Write(m_vram_bank);
    state.Write(m_wram_bank);
    if (m_mbc) {
        m_mbc->SaveState(state);
    }
}

bool Memory::LoadState(StateBuffer& state) {
    bool ok = state.ReadBytes(m_wram.data(), m_wram.size()) &&
              state.ReadBytes(m_vram.data(), m_vram.size()) &&
              state.ReadBytes(m_oam.data(), m_oam.size()) &&
              state.ReadBytes(m_hram.data(), m_hram.size()) &&
              state.ReadBytes(m_io.data(), m_io.size()) &&
              state.Read(m_ie_register) &&
              state.Read(m_vram_bank) &&
              state.Read(m_wram_bank);
    if (ok && m_mbc) {
        ok = m_mbc->LoadState(state);
    }
    if (ok) {
        UpdateBankedPageTables();
    }
    return ok;
}
