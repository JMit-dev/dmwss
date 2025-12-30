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
    // The system counter increments every M-cycle (4 T-cycles)
    // Process in M-cycle increments
    for (u32 i = 0; i < cycles; i += 4) {
        // Check for timer falling edge BEFORE incrementing
        if (IsTimerEnabled()) {
            bool old_bit = GetTimerBit(m_div_counter);
            m_div_counter++;  // Increment by 1 M-cycle
            bool new_bit = GetTimerBit(m_div_counter);

            if (old_bit && !new_bit) {
                // Falling edge detected - increment TIMA
                m_tima++;

                if (m_tima == 0) {
                    // Timer overflow - reload from TMA and request interrupt
                    m_tima = m_tma;
                    m_memory.RequestInterrupt(0x04);
                }
            }
        } else {
            // Timer disabled, just increment counter
            m_div_counter++;
        }
    }
}

void Timer::UpdateDIV(u32 cycles) {
    // No longer used - kept for compatibility
}

void Timer::UpdateTIMA(u32 cycles) {
    // No longer used - kept for compatibility
}

u8 Timer::GetBitPosition() const {
    switch (m_tac & 0x03) {
        case 0: return 7;  // 256 M-cycles -> bit 7 (2^8 = 256)
        case 1: return 1;  // 4 M-cycles -> bit 1 (2^2 = 4)
        case 2: return 3;  // 16 M-cycles -> bit 3 (2^4 = 16)
        case 3: return 5;  // 64 M-cycles -> bit 5 (2^6 = 64)
    }
    return 7;
}

bool Timer::GetTimerBit(u16 counter) const {
    // Select which bit to check based on TAC frequency select
    // TAC bits 0-1 select the bit from the system counter
    // Counter increments every M-cycle, so bit N gives period of 2^(N+1) M-cycles
    u8 bit_position = GetBitPosition();
    return (counter >> bit_position) & 1;
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
            // If the currently selected bit is 1, this causes a falling edge
            if (IsTimerEnabled() && GetTimerBit(m_div_counter)) {
                m_tima++;
                if (m_tima == 0) {
                    m_tima = m_tma;
                    m_memory.RequestInterrupt(0x04);
                }
            }
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
            // Writing to TIMA just sets the value, doesn't affect internal counter
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
            u8 old_tac = m_tac;
            bool old_bit = IsTimerEnabled() && GetTimerBit(m_div_counter);

            m_tac = value & 0x07;  // Only bottom 3 bits writable

            bool new_bit = IsTimerEnabled() && GetTimerBit(m_div_counter);

            // Detect falling edge: old bit was 1, new bit is 0
            if (old_bit && !new_bit) {
                m_tima++;
                if (m_tima == 0) {
                    m_tima = m_tma;
                    m_memory.RequestInterrupt(0x04);
                }
            }
        }
    );
}
