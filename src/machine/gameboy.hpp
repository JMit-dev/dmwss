#pragma once
#include "../core/types.hpp"
#include "../core/scheduler.hpp"
#include "../core/memory.hpp"
#include "../core/cpu.hpp"
#include "../core/ppu.hpp"
#include "../core/apu.hpp"
#include "../core/timer.hpp"
#include "../core/serial.hpp"
#include "cheats.hpp"
#include <string>
#include <vector>
#include <memory>

class GameBoy {
public:
    GameBoy();
    ~GameBoy();

    // ROM loading
    bool LoadROM(const std::string& path);
    bool LoadROM(const std::vector<u8>& rom_data);

    // Force dual-mode carts to run as a DMG (set before LoadROM);
    // CGB-only carts ignore this
    void SetForceDMG(bool force) { m_force_dmg = force; }

    // Apply the CGB boot ROM's colorization palettes to a DMG game
    // (no-op for CGB-mode games, which drive their own palettes)
    void ApplyBootColorization();

    // Provide a boot ROM image; classified by size (256 bytes = DMG,
    // 2304 bytes = CGB). The matching image runs on subsequent ROM
    // loads/resets, showing the real logo scroll before the game starts.
    void SetBootROM(const std::vector<u8>& data);

    // Persist battery-backed cartridge RAM (no-op without a battery cart)
    void SaveBattery();

    // Save states (stored as .state1-.state9 files next to the ROM)
    static constexpr int STATE_SLOT_COUNT = 9;
    bool SaveStateToFile(int slot = 1);
    bool LoadStateFromFile(int slot = 1);
    bool StateSlotExists(int slot) const;

    // System control
    void Reset();
    void RunFrame();
    void Step();  // Run one instruction

    // Get framebuffer for rendering
    const u32* GetFramebuffer() const { return m_ppu->GetFramebuffer(); }
    bool IsFrameReady() const { return m_ppu->IsFrameReady(); }
    void ClearFrameReady() { m_ppu->ClearFrameReady(); }

    // Input (joypad)
    void SetJoypadState(u8 state) { m_joypad_state = state; }

    // Cheats: GameShark codes apply once per frame, Game Genie codes
    // patch ROM while enabled. Cleared on ROM load.
    bool AddCheat(const std::string& name, const std::string& code);
    void RemoveCheat(size_t index);
    void SetCheatEnabled(size_t index, bool enabled);
    const std::vector<Cheat>& GetCheats() const { return m_cheats; }

    // ROM header title (for per-game settings keys)
    std::string GetROMTitle() const;

    // Debug
    bool IsRunning() const { return m_running; }
    u32 GetCycleCount() const { return m_total_cycles; }

    // Component access for debugging
    CPU& GetCPU() { return *m_cpu; }
    PPU& GetPPU() { return *m_ppu; }
    APU& GetAPU() { return *m_apu; }
    Memory& GetMemory() { return *m_memory; }
    Serial& GetSerial() { return *m_serial; }

private:
    // Components (order matters for initialization)
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<Memory> m_memory;
    std::unique_ptr<CPU> m_cpu;
    std::unique_ptr<PPU> m_ppu;
    std::unique_ptr<APU> m_apu;
    std::unique_ptr<Timer> m_timer;
    std::unique_ptr<Serial> m_serial;

    // State
    bool m_running;
    bool m_force_dmg = false;
    u32 m_total_cycles;
    u8 m_joypad_state;
    u8 m_joypad_select;  // P1 bits 4-5: button group selection
    std::vector<u8> m_rom_data;
    std::string m_save_path;  // Battery RAM file (.sav next to the ROM)
    std::vector<Cheat> m_cheats;

    // Optional user-supplied boot ROM images
    std::vector<u8> m_dmg_boot;
    std::vector<u8> m_cgb_boot;

    // CGB infrared port (RP, 0xFF56): stubbed, no receiver attached
    u8 m_rp = 0;

    // CGB VRAM DMA (HDMA1-5): general-purpose transfers run instantly,
    // HBlank transfers copy one 16-byte block per visible HBlank
    u16 m_hdma_source = 0;
    u16 m_hdma_dest = 0;
    u8 m_hdma_blocks = 0;    // Remaining 16-byte blocks
    bool m_hdma_active = false;

    // Timing
    static constexpr u32 CYCLES_PER_FRAME = 70224;  // ~59.73 Hz

    // Initialize I/O handlers
    void RegisterIOHandlers();

    // Copy one 16-byte HDMA block and advance the transfer
    void HDMATransferBlock();

    // Path for a given save state slot, derived from m_save_path
    std::string StateSlotPath(int slot) const;

    // Apply enabled GameShark cheats (called once per frame)
    void ApplyGameSharkCheats();
};
