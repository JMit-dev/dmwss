#include "gameboy.hpp"
#include <spdlog/spdlog.h>
#include <fstream>

GameBoy::GameBoy()
    : m_running(false)
    , m_total_cycles(0)
    , m_joypad_state(0xFF) {

    // Create components in dependency order
    m_scheduler = std::make_unique<Scheduler>();
    m_memory = std::make_unique<Memory>();
    m_cpu = std::make_unique<CPU>(*m_memory, *m_scheduler);
    m_ppu = std::make_unique<PPU>(*m_memory, *m_scheduler);
    m_timer = std::make_unique<Timer>(*m_memory, *m_scheduler);

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

    m_scheduler->Reset();
    m_memory->Reset();
    m_cpu->Reset();
    m_ppu->Reset();
    m_timer->Reset();

    m_running = true;
}

void GameBoy::Step() {
    if (!m_running) return;

    // Execute one CPU instruction
    u32 cycles = m_cpu->Step();

    // Update PPU and Timer
    m_ppu->Step(cycles);
    m_timer->Step(cycles);

    // Advance scheduler
    m_scheduler->Advance(cycles);
    m_scheduler->ProcessEvents();

    m_total_cycles += cycles;
}

void GameBoy::RunFrame() {
    if (!m_running) return;

    u32 frame_cycles = 0;

    // Run until we've completed a full frame worth of cycles
    while (frame_cycles < CYCLES_PER_FRAME) {
        u32 cycles = m_cpu->Step();

        m_ppu->Step(cycles);
        m_timer->Step(cycles);
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

    // For now, we'll use minimal I/O handlers and let the default behavior handle most registers
    // The PPU and other components already register their own handlers
}
