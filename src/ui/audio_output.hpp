#pragma once
#include "../core/apu/apu.hpp"

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

private:
    APU& m_apu;
    ma_device* m_device;
    bool m_running;
};
