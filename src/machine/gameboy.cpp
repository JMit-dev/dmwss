#include "gameboy.hpp"
#include <spdlog/spdlog.h>
#include <fstream>

GameBoy::GameBoy()
    : m_running(false)
    , m_total_cycles(0)
    , m_joypad_state(0xFF)
    , m_joypad_select(0x30) {

    // Create components in dependency order
    m_scheduler = std::make_unique<Scheduler>();
    m_memory = std::make_unique<Memory>();
    m_cpu = std::make_unique<CPU>(*m_memory, *m_scheduler);
    m_ppu = std::make_unique<PPU>(*m_memory, *m_scheduler);
    m_apu = std::make_unique<APU>(*m_memory);
    m_timer = std::make_unique<Timer>(*m_memory, *m_scheduler);

    // The CPU drives time: PPU and Timer advance on every memory access so
    // mid-instruction reads/writes observe them at the correct cycle.
    // In CGB double-speed mode the PPU and APU run at half the CPU rate,
    // while the Timer stays on the CPU clock.
    m_cpu->SetTickCallback([this](u32 cycles) {
        u32 real_cycles = m_cpu->IsDoubleSpeed() ? (cycles / 2) : cycles;
        m_ppu->Step(real_cycles);
        m_apu->Step(real_cycles);
        m_timer->Step(cycles);
    });

    // The OAM corruption bug is DMG-only silicon behavior
    m_cpu->SetOAMBugCallback([this](OAMBugType type) {
        if (!m_cpu->IsCGBMode()) {
            m_ppu->TriggerOAMBug(type);
        }
    });

    RegisterIOHandlers();

    spdlog::info("GameBoy system initialized");
}

bool GameBoy::LoadROM(const std::string& path) {
    spdlog::info("Loading ROM: {}", path);

    // Read ROM file
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        spdlog::error("Failed to open ROM file: {}", path);
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    m_rom_data.resize(size);
    if (!file.read(reinterpret_cast<char*>(m_rom_data.data()), size)) {
        spdlog::error("Failed to read ROM file: {}", path);
        return false;
    }

    return LoadROM(m_rom_data);
}

bool GameBoy::LoadROM(const std::vector<u8>& rom_data) {
    if (rom_data.size() < 0x150) {
        spdlog::error("ROM too small (< 0x150 bytes)");
        return false;
    }

    m_rom_data = rom_data;

    // Parse ROM header
    std::string title;
    for (int i = 0; i < 16; i++) {
        char c = m_rom_data[0x134 + i];
        if (c == 0) break;  // Null terminator
        title += c;
    }

    u8 cartridge_type = m_rom_data[0x147];
    u8 rom_size = m_rom_data[0x148];
    u8 ram_size = m_rom_data[0x149];

    // Boot as a CGB only for CGB-exclusive carts (header 0x143 == 0xC0);
    // dual-mode carts (0x80) run in the emulator's native DMG mode
    bool cgb_only = (m_rom_data[0x143] == 0xC0);
    m_cpu->SetCGBMode(cgb_only);
    if (cgb_only) {
        spdlog::info("CGB-only cartridge, booting in CGB mode");
    }

    if (!title.empty()) {
        spdlog::info("ROM Title: {}", title);
    }
    spdlog::info("Cartridge Type: 0x{:02X}", cartridge_type);
    spdlog::info("ROM Size: {} KB", (32 << rom_size));
    spdlog::info("RAM Size: 0x{:02X}", ram_size);

    // Load ROM into memory
    if (!m_memory->LoadROM(m_rom_data.data(), m_rom_data.size())) {
        spdlog::error("Failed to load ROM into memory");
        return false;
    }

    Reset();
    m_running = true;

    return true;
}

void GameBoy::Reset() {
    spdlog::info("Resetting GameBoy");

    m_total_cycles = 0;
    m_joypad_state = 0xFF;
    m_joypad_select = 0x30;

    m_scheduler->Reset();
    m_memory->Reset();
    m_cpu->Reset();
    m_ppu->Reset();
    m_apu->Reset();
    m_timer->Reset();

    m_running = true;
}

void GameBoy::Step() {
    if (!m_running) return;

    // Execute one CPU instruction (PPU and Timer tick via the CPU's callback)
    u32 cycles = m_cpu->Step();

    // Advance scheduler
    m_scheduler->Advance(cycles);
    m_scheduler->ProcessEvents();

    m_total_cycles += cycles;
}

void GameBoy::RunFrame() {
    if (!m_running) return;

    u32 frame_cycles = 0;

    // Run until we've completed a full frame worth of cycles
    // (PPU and Timer tick via the CPU's callback). In double-speed mode
    // the CPU executes twice as many cycles per frame.
    u32 frame_cycle_target = CYCLES_PER_FRAME * (m_cpu->IsDoubleSpeed() ? 2 : 1);
    while (frame_cycles < frame_cycle_target) {
        u32 cycles = m_cpu->Step();

        m_scheduler->Advance(cycles);
        m_scheduler->ProcessEvents();

        frame_cycles += cycles;
        m_total_cycles += cycles;
    }
}

void GameBoy::RegisterIOHandlers() {
    // Note: I/O handlers should NOT call m_memory->Read/Write for I/O addresses
    // as that would cause infinite recursion. The Memory class handles storing
    // the value in m_io[] array before/after calling these handlers.

    // Joypad register (0xFF00 / P1)
    // m_joypad_state bits (0 = pressed): 0=Right 1=Left 2=Up 3=Down
    //                                    4=A 5=B 6=Select 7=Start
    m_memory->RegisterIOHandler(0xFF00,
        [this](u16) -> u8 {
            // Bits 4-5 select which button group appears in bits 0-3
            // (0 = group selected); unselected reads as all released
            u8 result = 0xC0 | m_joypad_select | 0x0F;
            if ((m_joypad_select & 0x10) == 0) {
                result &= 0xF0 | (m_joypad_state & 0x0F);         // D-pad
            }
            if ((m_joypad_select & 0x20) == 0) {
                result &= 0xF0 | ((m_joypad_state >> 4) & 0x0F);  // Buttons
            }
            return result;
        },
        [this](u16, u8 value) {
            // Only bits 4-5 are writable (button group select)
            m_joypad_select = value & 0x30;
        }
    );

    // Serial I/O registers (0xFF01-0xFF02)
    // Stub these out for now - most games don't use serial
    m_memory->RegisterIOHandler(0xFF01,
        [](u16) -> u8 { return 0xFF; },  // SB - Serial transfer data
        [](u16, u8) {}                    // Ignore writes
    );

    m_memory->RegisterIOHandler(0xFF02,
        [](u16) -> u8 { return 0x7E; },  // SC - Serial transfer control (bit 7=0, not transferring)
        [](u16, u8) {}                    // Ignore writes
    );

    // DMA - OAM DMA transfer (0xFF46): writing XX copies 0xXX00-0xXX9F
    // to OAM. Real DMA takes 160 M-cycles with the bus restricted to HRAM;
    // games spin in an HRAM wait loop anyway, so an instant copy suffices.
    m_memory->RegisterIOHandler(0xFF46,
        [](u16) -> u8 { return 0xFF; },
        [this](u16, u8 value) {
            u16 source = static_cast<u16>(value) << 8;
            u8* oam = m_memory->GetOAM();
            for (u16 i = 0; i < 160; i++) {
                oam[i] = m_memory->Read(source + i);
            }
        }
    );

    // KEY1 - CGB speed switch (0xFF4D)
    m_memory->RegisterIOHandler(0xFF4D,
        [this](u16) -> u8 {
            if (!m_cpu->IsCGBMode()) {
                return 0xFF;
            }
            return (m_cpu->IsDoubleSpeed() ? 0x80 : 0x00) | 0x7E |
                   (m_cpu->IsSpeedSwitchArmed() ? 0x01 : 0x00);
        },
        [this](u16, u8 value) {
            if (m_cpu->IsCGBMode()) {
                m_cpu->ArmSpeedSwitch((value & 0x01) != 0);
            }
        }
    );

    // PPU, APU and Timer register their own handlers in their constructors
}
