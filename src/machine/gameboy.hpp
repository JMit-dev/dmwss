#pragma once
#include "../core/types.hpp"
#include "../core/scheduler/scheduler.hpp"
#include "../core/memory/memory.hpp"
#include "../core/cpu/cpu.hpp"
#include "../core/ppu/ppu.hpp"
#include "../core/timer/timer.hpp"
#include <string>
#include <vector>
#include <memory>

class GameBoy {
public:
    GameBoy();
    ~GameBoy() = default;

    // ROM loading
    bool LoadROM(const std::string& path);
    bool LoadROM(const std::vector<u8>& rom_data);

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

    // Debug
    bool IsRunning() const { return m_running; }
    u32 GetCycleCount() const { return m_total_cycles; }

    // Component access for debugging
    CPU& GetCPU() { return *m_cpu; }
    PPU& GetPPU() { return *m_ppu; }
    Memory& GetMemory() { return *m_memory; }

private:
    // Components (order matters for initialization)
    std::unique_ptr<Scheduler> m_scheduler;
    std::unique_ptr<Memory> m_memory;
    std::unique_ptr<CPU> m_cpu;
    std::unique_ptr<PPU> m_ppu;
    std::unique_ptr<Timer> m_timer;

    // State
    bool m_running;
    u32 m_total_cycles;
    u8 m_joypad_state;
    std::vector<u8> m_rom_data;

    // Timing
    static constexpr u32 CYCLES_PER_FRAME = 70224;  // ~59.73 Hz

    // Initialize I/O handlers
    void RegisterIOHandlers();
};
