// Headless test ROM runner for blargg's Game Boy test suites.
//
// Runs a ROM until it reports a result or the frame limit is reached.
// Results are detected via both protocols blargg's tests use:
//   - Serial: the test prints text ending in "Passed" or "Failed"
//   - Memory: signature DE B0 61 at 0xA001-0xA003, status at 0xA000
//     (0x80 = running, 0x00 = pass, anything else = failure code)
//
// Usage: blargg_runner <rom.gb> [max_frames]
// Exit code: 0 = pass, 1 = fail or timeout, 2 = usage/load error
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>
#include "machine/gameboy.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <rom.gb> [max_frames]\n", argv[0]);
        return 2;
    }
    spdlog::set_level(spdlog::level::off);

    const int max_frames = (argc > 2) ? std::atoi(argv[2]) : 60 * 60;  // 60s emulated default

    // Load via the buffer overload so no .sav/.state files are created or
    // read next to the test ROMs (stale battery RAM would leak previous
    // results into the next run)
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "failed to open rom: %s\n", argv[1]);
        return 2;
    }
    std::vector<u8> rom(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(rom.data()), rom.size());

    GameBoy gameboy;
    // Blargg's suites test DMG behavior (e.g. the OAM corruption bug),
    // but the ROM headers are flagged CGB-compatible - run them as a DMG
    gameboy.SetForceDMG(true);
    if (!gameboy.LoadROM(rom)) {
        std::fprintf(stderr, "failed to load rom: %s\n", argv[1]);
        return 2;
    }

    // Capture serial output: tests write a byte to SB (0xFF01) then set
    // bit 7 of SC (0xFF02) to start the transfer
    std::string serial_out;
    u8 serial_byte = 0xFF;
    gameboy.GetMemory().RegisterIOHandler(0xFF01,
        [&](u16) -> u8 { return serial_byte; },
        [&](u16, u8 value) { serial_byte = value; });
    gameboy.GetMemory().RegisterIOHandler(0xFF02,
        [](u16) -> u8 { return 0x7E; },
        [&](u16, u8 value) {
            if (value & 0x80) {
                serial_out += static_cast<char>(serial_byte);
            }
        });

    int result = -1;  // -1 = no result yet
    int frames = 0;
    for (; frames < max_frames; frames++) {
        gameboy.RunFrame();

        if (serial_out.find("Passed") != std::string::npos) { result = 0; break; }
        if (serial_out.find("Failed") != std::string::npos) { result = 1; break; }

        Memory& memory = gameboy.GetMemory();
        if (memory.Read(0xA001) == 0xDE && memory.Read(0xA002) == 0xB0 &&
            memory.Read(0xA003) == 0x61) {
            u8 status = memory.Read(0xA000);
            if (status != 0x80) {
                std::string text;
                for (u16 address = 0xA004; address < 0xB000; address++) {
                    u8 c = memory.Read(address);
                    if (c == 0) break;
                    text += static_cast<char>(c);
                }
                if (!text.empty()) {
                    std::printf("%s\n", text.c_str());
                }
                result = (status == 0) ? 0 : 1;
                break;
            }
        }
    }

    if (!serial_out.empty()) {
        std::printf("%s\n", serial_out.c_str());
    }
    std::printf("frames=%d\n", frames);
    if (result == 0) {
        std::printf("RESULT: PASS\n");
    } else if (result == 1) {
        std::printf("RESULT: FAIL\n");
    } else {
        std::printf("RESULT: TIMEOUT\n");
    }
    return (result == 0) ? 0 : 1;
}
