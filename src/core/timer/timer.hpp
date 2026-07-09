#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include "../scheduler/scheduler.hpp"

class StateBuffer;

class Timer {
public:
    Timer(Memory& memory, Scheduler& scheduler);
    ~Timer() = default;

    void Reset();
    void Step(u32 cycles);

    // Save states
    void SaveState(StateBuffer& state) const;
    bool LoadState(StateBuffer& state);

private:
    Memory& m_memory;
    Scheduler& m_scheduler;

    // Timer registers
    u16 m_div_counter;      // Internal DIV counter (increments at 16384 Hz)
    u8 m_tima;              // Timer counter (0xFF05)
    u8 m_tma;               // Timer modulo (0xFF06)
    u8 m_tac;               // Timer control (0xFF07)

    // Timing
    u32 m_timer_counter;

    // Helper methods
    void UpdateDIV(u32 cycles);
    void UpdateTIMA(u32 cycles);
    u32 GetTimerFrequency() const;
    bool IsTimerEnabled() const;
    bool GetTimerBit(u16 counter) const;
    u8 GetBitPosition() const;

    // I/O register handlers
    void RegisterIOHandlers();
};
