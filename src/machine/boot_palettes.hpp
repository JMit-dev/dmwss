#pragma once
#include "../core/types.hpp"
#include <array>
#include <vector>

// The CGB boot ROM assigns DMG games three colorization palettes (BG,
// OBJ0, OBJ1) chosen from the cartridge header: Nintendo-published games
// are looked up by title checksum, everything else gets the default set.
// Algorithm and data transcribed from the boot ROM disassembly
// (ISSOtm/gb-bootroms, src/cgb.asm).
struct BootColorization {
    std::array<u32, 4> bg;
    std::array<u32, 4> obj0;
    std::array<u32, 4> obj1;
};

// rom must hold at least the 0x150-byte header
BootColorization ComputeBootColorization(const std::vector<u8>& rom);
