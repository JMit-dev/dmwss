#pragma once
#include "../core/types.hpp"
#include <string>
#include <vector>
#include <utility>

// A parsed cheat code. Two formats are supported:
//   GameShark (8 hex digits, 01VVAAAA): writes value VV to RAM address
//     AAAA (byte-swapped in the code) once per frame
//   Game Genie (ABC-DEF or ABC-DEF-GHI): patches ROM address with a new
//     byte; 9-digit codes carry a compare byte that selects which banks
//     to patch
struct Cheat {
    enum class Type { GAMESHARK, GAME_GENIE };

    std::string name;
    std::string code;      // Normalized (uppercase, dashes stripped)
    bool enabled = true;
    Type type;
    u16 address;
    u8 value;
    bool has_compare = false;
    u8 compare = 0;

    // Game Genie only: patched ROM offsets with original bytes, kept so
    // disabling the cheat can restore them
    std::vector<std::pair<u32, u8>> rom_patch;
};

// Parses a cheat code string into `out`. Accepts GameShark and Game Genie
// formats; dashes, spaces, and case are ignored. Returns false if the
// code is malformed.
bool ParseCheatCode(const std::string& code, Cheat& out);
