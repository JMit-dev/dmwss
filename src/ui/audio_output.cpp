#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#include <miniaudio.h>

#include "audio_output.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

void AudioOutput::DataCallback(ma_device* device, void* output, const void*,
                                ma_uint32 frame_count) {
    AudioOutput* self = static_cast<AudioOutput*>(device->pUserData);
    s16* out = static_cast<s16*>(output);

    size_t got = self->m_apu.ReadSamples(out, frame_count);
    if (got < frame_count) {
        // Underrun: pad with silence
        std::memset(out + got * 2, 0, (frame_count - got) * 2 * sizeof(s16));
    }

    bool muted = self->m_muted.load(std::memory_order_relaxed);
    float volume = muted ? 0.0f : self->m_volume.load(std::memory_order_relaxed);
    if (volume != 1.0f) {
        for (ma_uint32 i = 0; i < frame_count * 2; i++) {
            out[i] = static_cast<s16>(out[i] * volume);
        }
    }
}

AudioOutput::AudioOutput(APU& apu)
    : m_apu(apu)
    , m_device(new ma_device)
    , m_running(false) {
}

AudioOutput::~AudioOutput() {
    Stop();
    delete m_device;
}

bool AudioOutput::Start() {
    if (m_running) {
        return true;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = APU::OUTPUT_SAMPLE_RATE;
    config.dataCallback = DataCallback;
    config.pUserData = this;

    if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
        spdlog::error("Failed to initialize audio device");
        return false;
    }
    if (ma_device_start(m_device) != MA_SUCCESS) {
        spdlog::error("Failed to start audio device");
        ma_device_uninit(m_device);
        return false;
    }

    m_running = true;
    spdlog::info("Audio output started ({} Hz stereo)", APU::OUTPUT_SAMPLE_RATE);
    return true;
}

void AudioOutput::Stop() {
    if (m_running) {
        ma_device_uninit(m_device);
        m_running = false;
    }
}
