#pragma once
#include "../core/apu.hpp"
#include <atomic>

struct ma_device;

// Plays the APU's sample ring buffer through the default audio device
// using miniaudio. The device callback runs on the audio thread and only
// touches APU::ReadSamples, which is single-producer/single-consumer safe.
class AudioOutput {
public:
    explicit AudioOutput(APU& apu);
    ~AudioOutput();

    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    bool Start();
    void Stop();

    void SetMuted(bool muted) { m_muted.store(muted, std::memory_order_relaxed); }
    bool IsMuted() const { return m_muted.load(std::memory_order_relaxed); }

    // 0.0 - 1.0
    void SetVolume(float volume) { m_volume.store(volume, std::memory_order_relaxed); }
    float GetVolume() const { return m_volume.load(std::memory_order_relaxed); }

private:
    static void DataCallback(ma_device* device, void* output, const void* input,
                              unsigned int frame_count);

    APU& m_apu;
    ma_device* m_device;
    bool m_running;
    std::atomic<bool> m_muted{false};
    std::atomic<float> m_volume{1.0f};
};
