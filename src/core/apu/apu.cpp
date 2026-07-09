#include "apu.hpp"
#include <spdlog/spdlog.h>

APU::APU(Memory& memory)
    : m_memory(memory)
    , m_power(true)
    , m_fs_counter(0)
    , m_fs_step(0) {
    RegisterIOHandlers();
    Reset();
}

void APU::Reset() {
    for (int i = 0; i < 4; i++) {
        m_channels[i] = {};
        m_channels[i].max_length = (i == 2) ? 256 : 64;
        m_channels[i].length = m_channels[i].max_length;
    }

    // Boot state: APU powered on with channel 1 active (NR52 = 0xF1)
    m_power = true;
    m_channels[0].active = true;
    m_channels[0].dac_enabled = true;

    m_fs_counter = 0;
    m_fs_step = 0;

    spdlog::debug("APU reset");
}

void APU::Step(u32 cycles) {
    if (!m_power) {
        return;
    }

    m_fs_counter += cycles;
    while (m_fs_counter >= FRAME_SEQUENCER_PERIOD) {
        m_fs_counter -= FRAME_SEQUENCER_PERIOD;

        // Length counters clock on steps 0, 2, 4, 6
        if ((m_fs_step & 1) == 0) {
            ClockLengthCounters();
        }
        m_fs_step = (m_fs_step + 1) & 7;
    }
}

void APU::ClockLengthCounters() {
    for (auto& channel : m_channels) {
        if (channel.length_enable && channel.length > 0) {
            channel.length--;
            if (channel.length == 0) {
                channel.active = false;
            }
        }
    }
}

void APU::WriteLength(int channel, u8 value) {
    Channel& ch = m_channels[channel];
    u16 mask = ch.max_length - 1;
    ch.length = ch.max_length - (value & mask);
}

void APU::WriteDAC(int channel, bool dac_on) {
    Channel& ch = m_channels[channel];
    ch.dac_enabled = dac_on;
    if (!dac_on) {
        ch.active = false;
    }
}

void APU::WriteControl(int channel, u8 value) {
    Channel& ch = m_channels[channel];
    ch.length_enable = (value & 0x40) != 0;

    if (value & 0x80) {  // Trigger
        if (ch.length == 0) {
            ch.length = ch.max_length;
        }
        ch.active = ch.dac_enabled;
    }
}

void APU::RegisterIOHandlers() {
    // Length registers: NR11, NR21, NR31, NR41
    static constexpr u16 LENGTH_REGS[4] = {0xFF11, 0xFF16, 0xFF1B, 0xFF20};
    // DAC registers: NR12, NR22, NR30, NR42
    static constexpr u16 DAC_REGS[4] = {0xFF12, 0xFF17, 0xFF1A, 0xFF21};
    // Control registers: NR14, NR24, NR34, NR44
    static constexpr u16 CONTROL_REGS[4] = {0xFF14, 0xFF19, 0xFF1E, 0xFF23};

    for (int i = 0; i < 4; i++) {
        // Local copies for lambda capture
        const int channel = i;

        m_memory.RegisterIOHandler(LENGTH_REGS[i],
            [this, channel](u16) -> u8 { return 0xFF; },
            [this, channel](u16, u8 value) {
                // Length counters are writable even with APU power off (DMG)
                WriteLength(channel, value);
            }
        );

        m_memory.RegisterIOHandler(DAC_REGS[i],
            [this, channel](u16) -> u8 { return 0xFF; },
            [this, channel](u16, u8 value) {
                if (!m_power) return;
                // Wave channel DAC is bit 7; others use the upper 5 bits
                bool dac_on = (channel == 2) ? ((value & 0x80) != 0)
                                             : ((value & 0xF8) != 0);
                WriteDAC(channel, dac_on);
            }
        );

        m_memory.RegisterIOHandler(CONTROL_REGS[i],
            [this, channel](u16) -> u8 { return 0xFF; },
            [this, channel](u16, u8 value) {
                if (!m_power) return;
                WriteControl(channel, value);
            }
        );
    }

    // NR52 - APU power / channel status (0xFF26)
    m_memory.RegisterIOHandler(0xFF26,
        [this](u16) -> u8 {
            u8 status = 0;
            for (int i = 0; i < 4; i++) {
                if (m_channels[i].active) {
                    status |= (1 << i);
                }
            }
            return (m_power ? 0x80 : 0x00) | 0x70 | status;
        },
        [this](u16, u8 value) {
            bool new_power = (value & 0x80) != 0;
            if (m_power && !new_power) {
                // Power off disables all channels and clears register state
                // (length counters are preserved on DMG)
                for (auto& channel : m_channels) {
                    channel.active = false;
                    channel.dac_enabled = false;
                    channel.length_enable = false;
                }
            }
            if (!m_power && new_power) {
                m_fs_step = 0;
                m_fs_counter = 0;
            }
            m_power = new_power;
        }
    );
}
