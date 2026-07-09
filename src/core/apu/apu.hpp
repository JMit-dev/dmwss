#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"

// Minimal APU: register behavior, frame sequencer, length counters and
// channel status (NR52). No sound synthesis yet - this is enough for
// software that gates on channel status (e.g. blargg's test helpers,
// which measure time by waiting for a length counter to expire).
class APU {
public:
    explicit APU(Memory& memory);
    ~APU() = default;

    void Reset();

    // Advance by t-cycles at the real-time rate (callers halve the CPU
    // cycle count in CGB double-speed mode)
    void Step(u32 cycles);

private:
    // Frame sequencer: 512 Hz, length counters clock on even steps (256 Hz)
    static constexpr u32 FRAME_SEQUENCER_PERIOD = 8192;

    struct Channel {
        bool active;         // NR52 status bit
        bool dac_enabled;    // Upper 5 bits of NRx2 (bit 7 of NR30 for wave)
        bool length_enable;  // NRx4 bit 6
        u16 length;          // Remaining length counter ticks
        u16 max_length;      // 64 for square/noise, 256 for wave
    };

    Memory& m_memory;
    Channel m_channels[4];
    bool m_power;
    u32 m_fs_counter;
    u8 m_fs_step;

    void RegisterIOHandlers();
    void ClockLengthCounters();
    void WriteLength(int channel, u8 value);
    void WriteDAC(int channel, bool dac_on);
    void WriteControl(int channel, u8 value);
};
