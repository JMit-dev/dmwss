#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include <array>
#include <atomic>

// APU: four channels (square+sweep, square, wave, noise) with register
// behavior accurate enough for blargg's dmg_sound tests, plus stereo
// sample generation into a lock-free ring buffer for the audio backend.
class APU {
public:
    explicit APU(Memory& memory);
    ~APU() = default;

    void Reset();

    // Advance by t-cycles at the real-time rate (callers halve the CPU
    // cycle count in CGB double-speed mode)
    void Step(u32 cycles);

    // Audio output: interleaved stereo s16 frames at OUTPUT_SAMPLE_RATE.
    // ReadSamples is safe to call from the audio thread (single producer,
    // single consumer).
    static constexpr u32 OUTPUT_SAMPLE_RATE = 48000;
    size_t ReadSamples(s16* out, size_t max_frames);

private:
    // Frame sequencer: 512 Hz. Steps 0/2/4/6 clock length, 2/6 clock
    // sweep, 7 clocks envelopes.
    static constexpr u32 FRAME_SEQUENCER_PERIOD = 8192;
    static constexpr u32 CLOCK_RATE = 4194304;

    struct Channel {
        bool active;
        bool dac_enabled;
        bool length_enable;
        u16 length;
        u16 max_length;

        // Envelope (square 1/2 and noise)
        u8 volume;        // Current volume 0-15
        u8 env_period;
        bool env_add;
        u8 env_timer;

        // Frequency/waveform
        u16 frequency;    // 11-bit for squares/wave; raw NR43 for noise
        s32 timer;        // T-cycles until next waveform step
        u8 output;        // Current digital output 0-15
    };

    Memory& m_memory;
    bool m_power;
    u32 m_fs_counter;
    u8 m_fs_step;         // Next frame sequencer step to fire

    Channel m_ch[4];

    // Square duty
    u8 m_duty[2];         // NRx1 bits 6-7
    u8 m_duty_pos[2];

    // Channel 1 sweep
    u8 m_sweep_period;
    u8 m_sweep_shift;
    bool m_sweep_negate;
    u16 m_sweep_shadow;
    u8 m_sweep_timer;
    bool m_sweep_enabled;
    bool m_sweep_negate_used;

    // Channel 3 wave
    u8 m_wave_ram[16];
    u8 m_wave_pos;        // 0-31 (4-bit samples)
    u8 m_wave_sample;     // Last byte fetched from wave RAM
    u8 m_wave_volume;     // NR32 bits 5-6
    bool m_wave_just_accessed;  // Fetch occurred during the current M-cycle
    u32 m_wave_fetch_dist;      // T-cycles from that fetch to the M-cycle end

    // Channel 4 noise
    u16 m_lfsr;

    // Panning / master volume
    u8 m_nr50;
    u8 m_nr51;

    // Raw register values for read-back
    u8 m_regs[0x20];      // 0xFF10-0xFF2F

    // Sample generation
    u32 m_sample_counter;
    static constexpr size_t RING_SIZE = 32768;  // Frames (power of two)
    std::array<s16, RING_SIZE * 2> m_ring;
    std::atomic<size_t> m_ring_write{0};
    std::atomic<size_t> m_ring_read{0};

    void RegisterIOHandlers();
    u8 ReadRegister(u16 address);
    void WriteRegister(u16 address, u8 value);

    void StepFrameSequencer();
    void ClockLengthCounters();
    void ClockSweep();
    void ClockEnvelopes();
    void StepChannelTimers(u32 cycles);
    void GenerateSamples(u32 cycles);

    void WriteLength(int channel, u8 value);
    void WriteEnvelope(int channel, u8 value);
    void WriteControl(int channel, u8 value);
    void Trigger(int channel);
    void PowerOff();

    u16 SweepCalculate();
    u32 NoisePeriod() const;

    // True if the next frame sequencer step will not clock length
    // (the "first half" of the length period, for the extra-clock quirk)
    bool NextStepSkipsLength() const { return (m_fs_step & 1) != 0; }
};
