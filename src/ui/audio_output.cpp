#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_DECODING
#include <miniaudio.h>

#include "audio_output.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

static void DataCallback(ma_device* device, void* output, const void*,
                         ma_uint32 frame_count) {
    APU* apu = static_cast<APU*>(device->pUserData);
    s16* out = static_cast<s16*>(output);

    size_t got = apu->ReadSamples(out, frame_count);
    if (got < frame_count) {
        // Underrun: pad with silence
        std::memset(out + got * 2, 0, (frame_count - got) * 2 * sizeof(s16));
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
    config.pUserData = &m_apu;

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
