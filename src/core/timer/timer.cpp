#include "timer.hpp"
#include <spdlog/spdlog.h>

Timer::Timer(Memory& memory, Scheduler& scheduler)
    : m_memory(memory)
    , m_scheduler(scheduler)
    , m_div_counter(0)
    , m_tima(0)
    , m_tma(0)
    , m_tac(0)
    , m_timer_counter(0) {

    RegisterIOHandlers();
    Reset();
}

void Timer::Reset() {
    m_div_counter = 0;
    m_tima = 0;
    m_tma = 0;
    m_tac = 0;
    m_timer_counter = 0;

    spdlog::debug("Timer reset");
}

void Timer::Step(u32 cycles) {
    UpdateDIV(cycles);

    if (IsTimerEnabled()) {
        UpdateTIMA(cycles);
    }
}

void Timer::UpdateDIV(u32 cycles) {
    // DIV increments at 16384 Hz (CPU clock / 256)
    // CPU runs at 4194304 Hz, so DIV increments every 256 cycles
    // The DIV register shows the upper 8 bits of m_div_counter
    m_div_counter += cycles;
}

void Timer::UpdateTIMA(u32 cycles) {
    u32 frequency = GetTimerFrequency();
    m_timer_counter += cycles;

    while (m_timer_counter >= frequency) {
        m_timer_counter -= frequency;

        m_tima++;

        if (m_tima == 0) {
            // Timer overflow - reload from TMA and request interrupt
            m_tima = m_tma;

            // Request timer interrupt (bit 2 of IF register)
            m_memory.RequestInterrupt(0x04);

            spdlog::debug("Timer overflow, TIMA reloaded from TMA: 0x{:02X}, interrupt requested", m_tma);
        }
    }
}

u32 Timer::GetTimerFrequency() const {
    // Timer frequency based on TAC bits 0-1
    // 00: 4096 Hz   (1024 cycles)
    // 01: 262144 Hz (16 cycles)
    // 10: 65536 Hz  (64 cycles)
    // 11: 16384 Hz  (256 cycles)

    switch (m_tac & 0x03) {
        case 0: return 1024;
        case 1: return 16;
        case 2: return 64;
        case 3: return 256;
        default: return 1024;
    }
}

bool Timer::IsTimerEnabled() const {
    // Timer is enabled when bit 2 of TAC is set
    return (m_tac & 0x04) != 0;
}

void Timer::RegisterIOHandlers() {
    // Note: I/O handlers must NOT call m_memory.Read/Write for I/O addresses
    // as that causes infinite recursion. We store values locally instead.

    // DIV - Divider Register (0xFF04)
    m_memory.RegisterIOHandler(0xFF04,
        [this](u16) -> u8 {
            // Return upper 8 bits of the 16-bit counter
            return static_cast<u8>(m_div_counter >> 8);
        },
        [this](u16, u8) {
            // Writing any value to DIV resets it to 0
            m_div_counter = 0;
        }
    );

    // TIMA - Timer Counter (0xFF05)
    m_memory.RegisterIOHandler(0xFF05,
        [this](u16) -> u8 {
            return m_tima;
        },
        [this](u16, u8 value) {
            m_tima = value;
            // Writing to TIMA resets the internal counter
            m_timer_counter = 0;
        }
    );

    // TMA - Timer Modulo (0xFF06)
    m_memory.RegisterIOHandler(0xFF06,
        [this](u16) -> u8 {
            return m_tma;
        },
        [this](u16, u8 value) {
            m_tma = value;
        }
    );

    // TAC - Timer Control (0xFF07)
    m_memory.RegisterIOHandler(0xFF07,
        [this](u16) -> u8 {
            return m_tac | 0xF8;  // Top 5 bits always set
        },
        [this](u16, u8 value) {
            bool was_enabled = IsTimerEnabled();
            m_tac = value & 0x07;  // Only bottom 3 bits writable

            // If timer state changed, reset the internal counter
            if (was_enabled != IsTimerEnabled()) {
                m_timer_counter = 0;
            }
        }
    );
}
