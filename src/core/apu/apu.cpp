#include "apu.hpp"
#include "../../machine/savestate.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

// Read-back OR masks for 0xFF10-0xFF2F (unused bits read as 1)
static constexpr u8 READ_MASKS[0x20] = {
    0x80, 0x3F, 0x00, 0xFF, 0xBF,  // NR10-NR14
    0xFF, 0x3F, 0x00, 0xFF, 0xBF,  // (FF15) NR21-NR24
    0x7F, 0xFF, 0x9F, 0xFF, 0xBF,  // NR30-NR34
    0xFF, 0xFF, 0x00, 0x00, 0xBF,  // (FF1F) NR41-NR44
    0x00, 0x00, 0x70,              // NR50-NR52
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // FF27-FF2F unused (9 bytes)
    0xFF, 0xFF, 0xFF, 0xFF
};

static constexpr u8 DUTY_TABLE[4] = {
    0x01,  // 12.5%: 00000001
    0x81,  // 25%:   10000001
    0x87,  // 50%:   10000111
    0x7E   // 75%:   01111110
};

APU::APU(Memory& memory)
    : m_memory(memory) {
    RegisterIOHandlers();
    Reset();
}

void APU::Reset() {
    m_power = true;
    m_fs_counter = 0;
    m_fs_step = 0;

    for (int i = 0; i < 4; i++) {
        m_ch[i] = {};
        m_ch[i].max_length = (i == 2) ? 256 : 64;
        m_ch[i].length = m_ch[i].max_length;
    }
    m_duty[0] = m_duty[1] = 0;
    m_duty_pos[0] = m_duty_pos[1] = 0;

    m_sweep_period = m_sweep_shift = 0;
    m_sweep_negate = false;
    m_sweep_shadow = 0;
    m_sweep_timer = 0;
    m_sweep_enabled = false;
    m_sweep_negate_used = false;

    std::memset(m_wave_ram, 0, sizeof(m_wave_ram));
    m_wave_pos = 0;
    m_wave_sample = 0;
    m_wave_volume = 0;
    m_wave_just_accessed = false;
    m_wave_fetch_dist = 0;

    m_lfsr = 0x7FFF;

    m_nr50 = 0x77;
    m_nr51 = 0xF3;
    std::memset(m_regs, 0, sizeof(m_regs));

    // Boot state: channel 1 active (NR52 = 0xF1)
    m_ch[0].active = true;
    m_ch[0].dac_enabled = true;

    m_sample_counter = 0;
    m_ring_write.store(0);
    m_ring_read.store(0);

    spdlog::debug("APU reset");
}

void APU::Step(u32 cycles) {
    m_wave_just_accessed = false;

    if (m_power) {
        StepChannelTimers(cycles);

        m_fs_counter += cycles;
        while (m_fs_counter >= FRAME_SEQUENCER_PERIOD) {
            m_fs_counter -= FRAME_SEQUENCER_PERIOD;
            StepFrameSequencer();
        }
    }

    GenerateSamples(cycles);
}

void APU::StepFrameSequencer() {
    if ((m_fs_step & 1) == 0) {
        ClockLengthCounters();
    }
    if (m_fs_step == 2 || m_fs_step == 6) {
        ClockSweep();
    }
    if (m_fs_step == 7) {
        ClockEnvelopes();
    }
    m_fs_step = (m_fs_step + 1) & 7;
}

void APU::ClockLengthCounters() {
    for (auto& ch : m_ch) {
        if (ch.length_enable && ch.length > 0) {
            ch.length--;
            if (ch.length == 0) {
                ch.active = false;
            }
        }
    }
}

u16 APU::SweepCalculate() {
    u16 delta = m_sweep_shadow >> m_sweep_shift;
    u16 result;
    if (m_sweep_negate) {
        result = m_sweep_shadow - delta;
        m_sweep_negate_used = true;
    } else {
        result = m_sweep_shadow + delta;
    }
    if (result > 2047) {
        m_ch[0].active = false;
    }
    return result;
}

void APU::ClockSweep() {
    if (m_sweep_timer > 0) {
        m_sweep_timer--;
    }
    if (m_sweep_timer == 0) {
        m_sweep_timer = m_sweep_period ? m_sweep_period : 8;
        if (m_sweep_enabled && m_sweep_period) {
            u16 result = SweepCalculate();
            if (result <= 2047 && m_sweep_shift) {
                m_sweep_shadow = result;
                m_ch[0].frequency = result;
                // Frequency write-back is visible in NR13/NR14
                m_regs[0x03] = result & 0xFF;
                m_regs[0x04] = (m_regs[0x04] & 0xF8) | ((result >> 8) & 0x07);
                SweepCalculate();  // Second overflow check
            }
        }
    }
}

void APU::ClockEnvelopes() {
    for (int i : {0, 1, 3}) {
        Channel& ch = m_ch[i];
        if (ch.env_period == 0) {
            continue;
        }
        if (ch.env_timer > 0) {
            ch.env_timer--;
        }
        if (ch.env_timer == 0) {
            ch.env_timer = ch.env_period;
            if (ch.env_add && ch.volume < 15) {
                ch.volume++;
            } else if (!ch.env_add && ch.volume > 0) {
                ch.volume--;
            }
        }
    }
}

u32 APU::NoisePeriod() const {
    static constexpr u32 DIVISORS[8] = {8, 16, 32, 48, 64, 80, 96, 112};
    u8 nr43 = static_cast<u8>(m_ch[3].frequency);
    return DIVISORS[nr43 & 0x07] << (nr43 >> 4);
}

void APU::StepChannelTimers(u32 cycles) {
    // Square channels
    for (int i = 0; i < 2; i++) {
        Channel& ch = m_ch[i];
        ch.timer -= static_cast<s32>(cycles);
        while (ch.timer <= 0) {
            ch.timer += (2048 - ch.frequency) * 4;
            m_duty_pos[i] = (m_duty_pos[i] + 1) & 7;
        }
        bool high = (DUTY_TABLE[m_duty[i]] >> m_duty_pos[i]) & 1;
        ch.output = (ch.active && high) ? ch.volume : 0;
    }

    // Wave channel - tracks the exact T-cycle of the last wave RAM fetch
    // so CPU accesses can be gated to the DMG's narrow access window
    {
        Channel& ch = m_ch[2];
        if (ch.active) {
            u32 consumed = 0;
            while (ch.timer <= static_cast<s32>(cycles - consumed)) {
                consumed += ch.timer;
                ch.timer = (2048 - ch.frequency) * 2;
                m_wave_pos = (m_wave_pos + 1) & 31;
                m_wave_sample = m_wave_ram[m_wave_pos >> 1];
                m_wave_just_accessed = true;
                m_wave_fetch_dist = cycles - consumed;
            }
            ch.timer -= (cycles - consumed);

            u8 sample = (m_wave_pos & 1) ? (m_wave_sample & 0x0F)
                                         : (m_wave_sample >> 4);
            ch.output = m_wave_volume ? (sample >> (m_wave_volume - 1)) : 0;
        } else {
            ch.output = 0;
        }
    }

    // Noise channel
    {
        Channel& ch = m_ch[3];
        ch.timer -= static_cast<s32>(cycles);
        u32 period = NoisePeriod();
        while (ch.timer <= 0) {
            ch.timer += period;
            u16 bit = (m_lfsr ^ (m_lfsr >> 1)) & 1;
            m_lfsr = (m_lfsr >> 1) | (bit << 14);
            if (m_ch[3].frequency & 0x08) {  // 7-bit width mode
                m_lfsr = (m_lfsr & ~0x40) | (bit << 6);
            }
        }
        ch.output = (ch.active && !(m_lfsr & 1)) ? ch.volume : 0;
    }
}

void APU::GenerateSamples(u32 cycles) {
    m_sample_counter += cycles * OUTPUT_SAMPLE_RATE;
    while (m_sample_counter >= CLOCK_RATE) {
        m_sample_counter -= CLOCK_RATE;

        s32 left = 0;
        s32 right = 0;
        for (int i = 0; i < 4; i++) {
            // DAC output: digital 0-15 maps to analog +15..-15 (approx)
            s32 dac = m_ch[i].dac_enabled ? (15 - 2 * m_ch[i].output) : 0;
            if (m_nr51 & (1 << (i + 4))) left += dac;
            if (m_nr51 & (1 << i))      right += dac;
        }
        left *= 1 + ((m_nr50 >> 4) & 0x07);
        right *= 1 + (m_nr50 & 0x07);
        // Max magnitude: 4 channels * 15 * 8 = 480; scale to ~50% s16
        s16 l = static_cast<s16>(left * 32);
        s16 r = static_cast<s16>(right * 32);

        size_t w = m_ring_write.load(std::memory_order_relaxed);
        size_t rd = m_ring_read.load(std::memory_order_acquire);
        if (w - rd < RING_SIZE) {  // Drop samples when the ring is full
            m_ring[(w % RING_SIZE) * 2] = l;
            m_ring[(w % RING_SIZE) * 2 + 1] = r;
            m_ring_write.store(w + 1, std::memory_order_release);
        }
    }
}

size_t APU::ReadSamples(s16* out, size_t max_frames) {
    size_t rd = m_ring_read.load(std::memory_order_relaxed);
    size_t w = m_ring_write.load(std::memory_order_acquire);
    size_t available = w - rd;
    size_t count = (available < max_frames) ? available : max_frames;
    for (size_t i = 0; i < count; i++) {
        out[i * 2] = m_ring[((rd + i) % RING_SIZE) * 2];
        out[i * 2 + 1] = m_ring[((rd + i) % RING_SIZE) * 2 + 1];
    }
    m_ring_read.store(rd + count, std::memory_order_release);
    return count;
}

void APU::WriteLength(int channel, u8 value) {
    Channel& ch = m_ch[channel];
    u16 mask = ch.max_length - 1;
    ch.length = ch.max_length - (value & mask);
}

void APU::WriteEnvelope(int channel, u8 value) {
    Channel& ch = m_ch[channel];
    ch.dac_enabled = (value & 0xF8) != 0;
    if (!ch.dac_enabled) {
        ch.active = false;
    }
    ch.env_period = value & 0x07;
    ch.env_add = (value & 0x08) != 0;
}

void APU::Trigger(int channel) {
    Channel& ch = m_ch[channel];
    bool was_active = ch.active;
    ch.active = ch.dac_enabled;

    if (ch.length == 0) {
        ch.length = ch.max_length;
        // Triggering with length enabled in the first half of the length
        // period reloads to max-1
        if (ch.length_enable && NextStepSkipsLength()) {
            ch.length--;
        }
    }

    switch (channel) {
        case 0: {
            ch.timer = (2048 - ch.frequency) * 4;
            ch.volume = m_regs[0x02] >> 4;
            ch.env_timer = ch.env_period;
            // Sweep init
            m_sweep_shadow = ch.frequency;
            m_sweep_timer = m_sweep_period ? m_sweep_period : 8;
            m_sweep_enabled = (m_sweep_period != 0) || (m_sweep_shift != 0);
            m_sweep_negate_used = false;
            if (m_sweep_shift != 0) {
                SweepCalculate();  // Immediate overflow check
            }
            break;
        }
        case 1:
            ch.timer = (2048 - ch.frequency) * 4;
            ch.volume = m_regs[0x07] >> 4;
            ch.env_timer = ch.env_period;
            break;
        case 2: {
            // DMG quirk: triggering while the channel is about to read a
            // wave byte (within 2 T-cycles) corrupts the start of wave RAM
            if (was_active && ch.timer <= 2) {
                u8 offset = ((m_wave_pos + 1) >> 1) & 0x0F;
                if (offset < 4) {
                    m_wave_ram[0] = m_wave_ram[offset];
                } else {
                    std::memcpy(&m_wave_ram[0], &m_wave_ram[offset & ~3], 4);
                }
            }
            ch.timer = (2048 - ch.frequency) * 2 + 6;  // Trigger delay
            m_wave_pos = 0;
            break;
        }
        case 3:
            ch.timer = NoisePeriod();
            ch.volume = m_regs[0x11] >> 4;
            ch.env_timer = ch.env_period;
            m_lfsr = 0x7FFF;
            break;
    }
}

void APU::WriteControl(int channel, u8 value) {
    Channel& ch = m_ch[channel];
    bool was_enabled = ch.length_enable;
    ch.length_enable = (value & 0x40) != 0;

    // Extra length clock when enabling while the next frame sequencer
    // step will not clock length
    if (!was_enabled && ch.length_enable && NextStepSkipsLength() &&
        ch.length > 0) {
        ch.length--;
        if (ch.length == 0 && !(value & 0x80)) {
            ch.active = false;
        }
    }

    if (value & 0x80) {
        Trigger(channel);
    }
}

void APU::PowerOff() {
    // Power off clears all registers and channel state; on DMG the length
    // counters are preserved
    for (int i = 0; i < 4; i++) {
        u16 length = m_ch[i].length;
        u16 max_length = m_ch[i].max_length;
        m_ch[i] = {};
        m_ch[i].length = length;
        m_ch[i].max_length = max_length;
    }
    m_duty[0] = m_duty[1] = 0;
    m_duty_pos[0] = m_duty_pos[1] = 0;
    m_sweep_period = m_sweep_shift = 0;
    m_sweep_negate = false;
    m_sweep_enabled = false;
    m_sweep_negate_used = false;
    m_wave_volume = 0;
    m_nr50 = 0;
    m_nr51 = 0;
    std::memset(m_regs, 0, sizeof(m_regs));
}

u8 APU::ReadRegister(u16 address) {
    // Wave RAM
    if (address >= 0xFF30 && address <= 0xFF3F) {
        if (m_ch[2].active) {
            // DMG: wave RAM is only readable if the channel fetched a byte
            // within the last 2 T-cycles of the current M-cycle (i.e. the
            // fetch coincides with the CPU's bus access)
            if (m_wave_just_accessed && m_wave_fetch_dist <= 1) {
                return m_wave_ram[m_wave_pos >> 1];
            }
            return 0xFF;
        }
        return m_wave_ram[address - 0xFF30];
    }

    u8 index = address - 0xFF10;
    u8 value = m_regs[index];

    // NR52: power + channel status
    if (address == 0xFF26) {
        value = m_power ? 0x80 : 0x00;
        for (int i = 0; i < 4; i++) {
            if (m_ch[i].active) {
                value |= (1 << i);
            }
        }
    }
    return value | READ_MASKS[index];
}

void APU::WriteRegister(u16 address, u8 value) {
    // Wave RAM
    if (address >= 0xFF30 && address <= 0xFF3F) {
        if (m_ch[2].active) {
            // DMG: writes only land within the same narrow window as reads
            if (m_wave_just_accessed && m_wave_fetch_dist <= 1) {
                m_wave_ram[m_wave_pos >> 1] = value;
            }
            return;
        }
        m_wave_ram[address - 0xFF30] = value;
        return;
    }

    // NR52 power control
    if (address == 0xFF26) {
        bool new_power = (value & 0x80) != 0;
        if (m_power && !new_power) {
            PowerOff();
        }
        if (!m_power && new_power) {
            m_fs_step = 0;
            m_fs_counter = 0;
        }
        m_power = new_power;
        return;
    }

    // While powered off only NRx1 length loads are writable (DMG)
    if (!m_power) {
        switch (address) {
            case 0xFF11: WriteLength(0, value); break;
            case 0xFF16: WriteLength(1, value); break;
            case 0xFF1B: WriteLength(2, value); break;
            case 0xFF20: WriteLength(3, value); break;
            default: break;
        }
        return;
    }

    u8 index = address - 0xFF10;
    m_regs[index] = value;

    switch (address) {
        case 0xFF10:  // NR10 - sweep
            // Clearing negate after a negate-mode calculation kills the channel
            if (m_sweep_negate_used && m_sweep_negate && !(value & 0x08)) {
                m_ch[0].active = false;
            }
            m_sweep_period = (value >> 4) & 0x07;
            m_sweep_negate = (value & 0x08) != 0;
            m_sweep_shift = value & 0x07;
            break;

        case 0xFF11: m_duty[0] = value >> 6; WriteLength(0, value); break;
        case 0xFF12: WriteEnvelope(0, value); break;
        case 0xFF13:
            m_ch[0].frequency = (m_ch[0].frequency & 0x0700) | value;
            break;
        case 0xFF14:
            m_ch[0].frequency = (m_ch[0].frequency & 0x00FF) | ((value & 0x07) << 8);
            WriteControl(0, value);
            break;

        case 0xFF16: m_duty[1] = value >> 6; WriteLength(1, value); break;
        case 0xFF17: WriteEnvelope(1, value); break;
        case 0xFF18:
            m_ch[1].frequency = (m_ch[1].frequency & 0x0700) | value;
            break;
        case 0xFF19:
            m_ch[1].frequency = (m_ch[1].frequency & 0x00FF) | ((value & 0x07) << 8);
            WriteControl(1, value);
            break;

        case 0xFF1A:  // NR30 - wave DAC
            m_ch[2].dac_enabled = (value & 0x80) != 0;
            if (!m_ch[2].dac_enabled) {
                m_ch[2].active = false;
            }
            break;
        case 0xFF1B: WriteLength(2, value); break;
        case 0xFF1C: m_wave_volume = (value >> 5) & 0x03; break;
        case 0xFF1D:
            m_ch[2].frequency = (m_ch[2].frequency & 0x0700) | value;
            break;
        case 0xFF1E:
            m_ch[2].frequency = (m_ch[2].frequency & 0x00FF) | ((value & 0x07) << 8);
            WriteControl(2, value);
            break;

        case 0xFF20: WriteLength(3, value); break;
        case 0xFF21: WriteEnvelope(3, value); break;
        case 0xFF22: m_ch[3].frequency = value; break;
        case 0xFF23: WriteControl(3, value); break;

        case 0xFF24: m_nr50 = value; break;
        case 0xFF25: m_nr51 = value; break;

        default:
            break;  // FF15, FF1F, FF27-FF2F: unused
    }
}

void APU::RegisterIOHandlers() {
    for (u16 address = 0xFF10; address <= 0xFF3F; address++) {
        m_memory.RegisterIOHandler(address,
            [this](u16 addr) -> u8 { return ReadRegister(addr); },
            [this](u16 addr, u8 value) { WriteRegister(addr, value); }
        );
    }
}

void APU::SaveState(StateBuffer& state) const {
    state.Write(m_power);
    state.Write(m_fs_counter);
    state.Write(m_fs_step);
    state.WriteBytes(m_ch, sizeof(m_ch));
    state.WriteBytes(m_duty, sizeof(m_duty));
    state.WriteBytes(m_duty_pos, sizeof(m_duty_pos));
    state.Write(m_sweep_period);
    state.Write(m_sweep_shift);
    state.Write(m_sweep_negate);
    state.Write(m_sweep_shadow);
    state.Write(m_sweep_timer);
    state.Write(m_sweep_enabled);
    state.Write(m_sweep_negate_used);
    state.WriteBytes(m_wave_ram, sizeof(m_wave_ram));
    state.Write(m_wave_pos);
    state.Write(m_wave_sample);
    state.Write(m_wave_volume);
    state.Write(m_lfsr);
    state.Write(m_nr50);
    state.Write(m_nr51);
    state.WriteBytes(m_regs, sizeof(m_regs));
}

bool APU::LoadState(StateBuffer& state) {
    bool ok = state.Read(m_power) &&
              state.Read(m_fs_counter) &&
              state.Read(m_fs_step) &&
              state.ReadBytes(m_ch, sizeof(m_ch)) &&
              state.ReadBytes(m_duty, sizeof(m_duty)) &&
              state.ReadBytes(m_duty_pos, sizeof(m_duty_pos)) &&
              state.Read(m_sweep_period) &&
              state.Read(m_sweep_shift) &&
              state.Read(m_sweep_negate) &&
              state.Read(m_sweep_shadow) &&
              state.Read(m_sweep_timer) &&
              state.Read(m_sweep_enabled) &&
              state.Read(m_sweep_negate_used) &&
              state.ReadBytes(m_wave_ram, sizeof(m_wave_ram)) &&
              state.Read(m_wave_pos) &&
              state.Read(m_wave_sample) &&
              state.Read(m_wave_volume) &&
              state.Read(m_lfsr) &&
              state.Read(m_nr50) &&
              state.Read(m_nr51) &&
              state.ReadBytes(m_regs, sizeof(m_regs));
    // Audio ring buffer restarts empty
    m_wave_just_accessed = false;
    m_wave_fetch_dist = 0;
    m_sample_counter = 0;
    m_ring_write.store(0);
    m_ring_read.store(0);
    return ok;
}
