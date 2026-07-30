// Emscripten entry points for the web build: a minimal C ABI over the
// emulator core, driven from JavaScript (see index.html).
#include <emscripten/emscripten.h>
#include <spdlog/spdlog.h>
#include <vector>
#include "machine/gameboy.hpp"

static GameBoy* g_gameboy = nullptr;

extern "C" {

// Returns 1 on success. data/size point into the wasm heap.
EMSCRIPTEN_KEEPALIVE
int dmwss_load_rom(const u8* data, int size) {
    if (!g_gameboy) {
        spdlog::set_level(spdlog::level::warn);
        g_gameboy = new GameBoy();
    }
    return g_gameboy->LoadROM(std::vector<u8>(data, data + size)) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
void dmwss_run_frame() {
    if (g_gameboy) g_gameboy->RunFrame();
}

// 160x144 RGBA pixels
EMSCRIPTEN_KEEPALIVE
const u32* dmwss_framebuffer() {
    return g_gameboy ? g_gameboy->GetFramebuffer() : nullptr;
}

// Bit clear = pressed: 0=Right 1=Left 2=Up 3=Down 4=A 5=B 6=Select 7=Start
EMSCRIPTEN_KEEPALIVE
void dmwss_set_joypad(int state) {
    if (g_gameboy) g_gameboy->SetJoypadState(static_cast<u8>(state));
}

// Fills out with interleaved stereo s16 frames at 48000 Hz; returns the
// number of frames written
EMSCRIPTEN_KEEPALIVE
int dmwss_read_audio(s16* out, int max_frames) {
    if (!g_gameboy) return 0;
    return static_cast<int>(
        g_gameboy->GetAPU().ReadSamples(out, static_cast<size_t>(max_frames)));
}

EMSCRIPTEN_KEEPALIVE
int dmwss_rom_loaded() {
    return (g_gameboy && g_gameboy->IsRunning()) ? 1 : 0;
}

}  // extern "C"
