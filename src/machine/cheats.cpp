#include "cheats.hpp"
#include <cctype>

// Converts a hex digit character to its value, or -1 if invalid
static int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParseCheatCode(const std::string& code, Cheat& out) {
    // Normalize: uppercase, strip separators
    std::string digits;
    for (char c : code) {
        if (c == '-' || c == ' ') continue;
        digits += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }

    std::vector<int> v(digits.size());
    for (size_t i = 0; i < digits.size(); i++) {
        v[i] = HexValue(digits[i]);
        if (v[i] < 0) return false;
    }

    if (digits.size() == 8) {
        // GameShark: TTVVAAAA, address stored low byte first
        out.type = Cheat::Type::GAMESHARK;
        u8 type_byte = static_cast<u8>((v[0] << 4) | v[1]);
        if (type_byte != 0x01) return false;  // Only RAM writes supported
        out.value = static_cast<u8>((v[2] << 4) | v[3]);
        u8 addr_low = static_cast<u8>((v[4] << 4) | v[5]);
        u8 addr_high = static_cast<u8>((v[6] << 4) | v[7]);
        out.address = static_cast<u16>((addr_high << 8) | addr_low);
        // GameShark writes target RAM (0xA000-0xDFFF)
        if (out.address < 0xA000 || out.address > 0xDFFF) return false;
        out.has_compare = false;
    } else if (digits.size() == 6 || digits.size() == 9) {
        // Game Genie: ABC-DEF(-GHI)
        //   value   = AB
        //   address = FCDE ^ 0xF000
        //   compare = rotate_right(GI, 2) ^ 0xBA  (9-digit codes; H is a
        //   dummy digit)
        out.type = Cheat::Type::GAME_GENIE;
        out.value = static_cast<u8>((v[0] << 4) | v[1]);
        out.address = static_cast<u16>(
            ((v[5] << 12) | (v[2] << 8) | (v[3] << 4) | v[4]) ^ 0xF000);
        if (out.address >= 0x8000) return false;  // ROM addresses only
        if (digits.size() == 9) {
            u8 cmp = static_cast<u8>((v[6] << 4) | v[8]);
            cmp = static_cast<u8>((cmp >> 2) | (cmp << 6));
            out.compare = cmp ^ 0xBA;
            out.has_compare = true;
        } else {
            out.has_compare = false;
        }
    } else {
        return false;
    }

    out.code = digits;
    return true;
}
