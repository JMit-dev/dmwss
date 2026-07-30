#include "ppu.hpp"
#include "../../machine/savestate.hpp"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>

PPU::PPU(Memory& memory, Scheduler& scheduler)
    : m_memory(memory)
    , m_scheduler(scheduler)
    , m_mode(Mode::OAM_SCAN)
    , m_cycle_counter(0)
    , m_scanline(0)
    , m_frame_ready(false)
    , m_sprite_count(0)
    , m_lcdc(0x91)
    , m_stat(0x00)
    , m_scy(0)
    , m_scx(0)
    , m_lyc(0)
    , m_bgp(0xFC)
    , m_obp0(0xFF)
    , m_obp1(0xFF)
    , m_wy(0)
    , m_wx(0) {
    Reset();
    RegisterIOHandlers();
}

void PPU::Reset() {
    m_framebuffer.fill(0xFFFFFFFF);  // White
    m_mode = Mode::OAM_SCAN;
    m_cycle_counter = 0;
    m_scanline = 0;
    m_window_line = 0;
    m_frame_ready = false;
    m_first_line = false;
    m_sprite_count = 0;

    spdlog::debug("PPU reset");
}

void PPU::Step(u32 cycles) {
    // If LCD is disabled, don't update
    if ((m_lcdc & LCDC_LCD_ENABLE) == 0) {
        return;
    }

    m_cycle_counter += cycles;

    switch (m_mode) {
        case Mode::OAM_SCAN: {
            if (m_cycle_counter >= OAM_SCAN_CYCLES) {
                m_cycle_counter -= OAM_SCAN_CYCLES;

                // Scan OAM for sprites on this line
                ScanOAM();

                // Move to drawing mode
                m_first_line = false;
                SetMode(Mode::DRAWING);
            }
            break;
        }

        case Mode::DRAWING: {
            if (m_cycle_counter >= DRAWING_CYCLES) {
                m_cycle_counter -= DRAWING_CYCLES;
                
                // Render the current scanline
                RenderScanline();
                
                // Move to HBlank
                SetMode(Mode::HBLANK);
            }
            break;
        }

        case Mode::HBLANK: {
            if (m_cycle_counter >= HBLANK_CYCLES) {
                m_cycle_counter -= HBLANK_CYCLES;
                
                // Move to next scanline
                m_scanline++;
                m_memory.Write(0xFF44, m_scanline);  // Update LY register
                
                // Check LYC=LY
                UpdateStatRegister();
                
                if (m_scanline >= SCREEN_HEIGHT) {
                    // Enter VBlank
                    SetMode(Mode::VBLANK);
                    m_frame_ready = true;
                    
                    // Request VBlank interrupt
                    m_memory.Write(0xFF0F, m_memory.Read(0xFF0F) | 0x01);
                } else {
                    // Next scanline - go to OAM scan
                    SetMode(Mode::OAM_SCAN);
                }
            }
            break;
        }

        case Mode::VBLANK: {
            if (m_cycle_counter >= CYCLES_PER_SCANLINE) {
                m_cycle_counter -= CYCLES_PER_SCANLINE;
                
                m_scanline++;
                m_memory.Write(0xFF44, m_scanline);
                
                // Check LYC=LY
                UpdateStatRegister();
                
                if (m_scanline >= SCANLINES_PER_FRAME) {
                    // Frame complete, restart from scanline 0
                    m_scanline = 0;
                    m_window_line = 0;
                    m_memory.Write(0xFF44, m_scanline);
                    SetMode(Mode::OAM_SCAN);
                }
            }
            break;
        }
    }
}

void PPU::SetMode(Mode mode) {
    m_mode = mode;
    
    // Update STAT register mode bits
    m_stat = (m_stat & 0xFC) | static_cast<u8>(mode);
    m_memory.Write(0xFF41, m_stat);
    
    // Check for STAT interrupts
    bool request_stat_int = false;
    
    switch (mode) {
        case Mode::HBLANK:
            request_stat_int = (m_stat & STAT_HBLANK_INT) != 0;
            // CGB HBlank DMA copies one 16-byte block per visible HBlank
            if (m_hblank_callback && m_scanline < SCREEN_HEIGHT) {
                m_hblank_callback();
            }
            break;
        case Mode::VBLANK:
            request_stat_int = (m_stat & STAT_VBLANK_INT) != 0;
            break;
        case Mode::OAM_SCAN:
            request_stat_int = (m_stat & STAT_OAM_INT) != 0;
            break;
        default:
            break;
    }
    
    if (request_stat_int) {
        m_memory.Write(0xFF0F, m_memory.Read(0xFF0F) | 0x02);  // STAT interrupt
    }
}

void PPU::UpdateStatRegister() {
    // Check LYC=LY
    bool lyc_equal = (m_scanline == m_lyc);
    
    if (lyc_equal) {
        m_stat |= STAT_LYC_EQUAL;
        
        // Request LYC interrupt if enabled
        if (m_stat & STAT_LYC_INT) {
            m_memory.Write(0xFF0F, m_memory.Read(0xFF0F) | 0x02);
        }
    } else {
        m_stat &= ~STAT_LYC_EQUAL;
    }
    
    m_memory.Write(0xFF41, m_stat);
}

// DMG OAM corruption bug (Pan Docs "OAM Corruption Bug"):
// OAM is 20 rows of 8 bytes (4 words, 16-bit data bus). During mode 2 the
// PPU accesses one row per M-cycle; a CPU-side OAM access or 16-bit
// increment/decrement in the OAM range during that M-cycle corrupts the
// row the PPU is about to access. Corruption during M-cycle N of mode 2
// affects row N+1, which is why the first row (objects 0-1) is never
// corrupted and the trigger during the last M-cycle (N=19) does nothing.

static u16 ReadOAMWord(const u8* oam, u32 row, u32 word) {
    u32 offset = row * 8 + word * 2;
    return static_cast<u16>(oam[offset]) | (static_cast<u16>(oam[offset + 1]) << 8);
}

static void WriteOAMWord(u8* oam, u32 row, u32 word, u16 value) {
    u32 offset = row * 8 + word * 2;
    oam[offset] = static_cast<u8>(value & 0xFF);
    oam[offset + 1] = static_cast<u8>(value >> 8);
}

void PPU::TriggerOAMBug(OAMBugType type) {
    if ((m_lcdc & LCDC_LCD_ENABLE) == 0) {
        return;
    }
    if (m_mode != Mode::OAM_SCAN || m_first_line) {
        return;
    }

    // The CPU triggers before ticking, so m_cycle_counter is at the start
    // of the M-cycle in which the access happens
    u32 row = (m_cycle_counter / 4) + 1;
    if (row >= 20) {
        return;
    }

    switch (type) {
        case OAMBugType::READ:         CorruptOAMRead(row); break;
        case OAMBugType::WRITE:        CorruptOAMWrite(row); break;
        case OAMBugType::READ_INC_DEC: CorruptOAMIncDec(row); break;
    }
}

void PPU::CorruptOAMWrite(u32 row) {
    u8* oam = m_memory.GetOAM();

    u16 a = ReadOAMWord(oam, row, 0);
    u16 b = ReadOAMWord(oam, row - 1, 0);
    u16 c = ReadOAMWord(oam, row - 1, 2);
    WriteOAMWord(oam, row, 0, ((a ^ c) & (b ^ c)) ^ c);

    // Last three words are copied from the preceding row
    for (u32 word = 1; word < 4; word++) {
        WriteOAMWord(oam, row, word, ReadOAMWord(oam, row - 1, word));
    }
}

void PPU::CorruptOAMRead(u32 row) {
    u8* oam = m_memory.GetOAM();

    u16 a = ReadOAMWord(oam, row, 0);
    u16 b = ReadOAMWord(oam, row - 1, 0);
    u16 c = ReadOAMWord(oam, row - 1, 2);
    WriteOAMWord(oam, row, 0, b | (a & c));

    for (u32 word = 1; word < 4; word++) {
        WriteOAMWord(oam, row, word, ReadOAMWord(oam, row - 1, word));
    }
}

void PPU::CorruptOAMIncDec(u32 row) {
    u8* oam = m_memory.GetOAM();

    // The complex pattern is skipped for the first four rows and the last
    if (row >= 4 && row < 19) {
        u16 a = ReadOAMWord(oam, row - 2, 0);
        u16 b = ReadOAMWord(oam, row - 1, 0);
        u16 c = ReadOAMWord(oam, row, 0);
        u16 d = ReadOAMWord(oam, row - 1, 2);
        WriteOAMWord(oam, row - 1, 0, (b & (a | c | d)) | (a & c & d));

        // The (corrupted) preceding row is copied to the current row and
        // to two rows before it
        for (u32 word = 0; word < 4; word++) {
            u16 value = ReadOAMWord(oam, row - 1, word);
            WriteOAMWord(oam, row, word, value);
            WriteOAMWord(oam, row - 2, word, value);
        }
    }

    // A normal read corruption applies regardless
    CorruptOAMRead(row);
}

void PPU::ScanOAM() {
    m_sprite_count = 0;
    
    // Get OAM data
    u8* oam = m_memory.GetOAM();
    
    // Sprite height (8 or 16 pixels)
    u8 sprite_height = (m_lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
    
    // Scan all 40 sprites
    for (u8 i = 0; i < 40 && m_sprite_count < 10; i++) {
        Sprite sprite;
        sprite.y = oam[i * 4 + 0];
        sprite.x = oam[i * 4 + 1];
        sprite.tile = oam[i * 4 + 2];
        sprite.flags = oam[i * 4 + 3];
        
        // Check if sprite is on this scanline
        s16 sprite_y = sprite.y - 16;
        s16 scanline = m_scanline;
        
        if (scanline >= sprite_y && scanline < sprite_y + sprite_height) {
            m_sprite_buffer[m_sprite_count++] = sprite;
        }
    }
}

void PPU::RenderScanline() {
    // Only render visible scanlines
    if (m_scanline >= SCREEN_HEIGHT) {
        return;
    }

    // In CGB mode LCDC bit 0 is master priority, not BG enable: the
    // background always renders
    bool bg_enabled = m_cgb_mode || (m_lcdc & LCDC_BG_ENABLE) != 0;

    if (bg_enabled) {
        RenderBackground(m_scanline);
        if (m_lcdc & LCDC_WIN_ENABLE) {
            RenderWindow(m_scanline);
        }
    } else {
        // DMG with BG disabled shows a blank (shade 0) line
        m_line_color.fill(0);
        m_line_priority.fill(false);
        for (u8 x = 0; x < SCREEN_WIDTH; x++) {
            m_framebuffer[m_scanline * SCREEN_WIDTH + x] = m_display_bg[0];
        }
    }

    if (m_lcdc & LCDC_OBJ_ENABLE) {
        RenderSprites(m_scanline);
    }
}

void PPU::RenderTileLayerPixel(u8 scanline, u8 screen_x, u16 tile_map_base,
                               bool signed_addressing, u8 map_x, u8 map_y) {
    u8* vram = m_memory.GetVRAM();  // Bank 0: tile maps and DMG tile data

    u8 tile_x = map_x / 8;
    u8 pixel_x = map_x % 8;
    u8 tile_y = map_y / 8;
    u8 pixel_y = map_y % 8;

    u16 tile_map_addr = tile_map_base + (tile_y * 32) + tile_x;
    u8 tile_index = vram[tile_map_addr];

    // CGB: the attribute byte lives at the same tilemap offset in bank 1
    const u8* tile_vram = vram;
    u8 palette = 0;
    bool priority = false;
    if (m_cgb_mode) {
        u8 attr = m_memory.GetVRAMBank(1)[tile_map_addr];
        palette = attr & 0x07;
        if (attr & 0x08) tile_vram = m_memory.GetVRAMBank(1);
        if (attr & 0x20) pixel_x = 7 - pixel_x;
        if (attr & 0x40) pixel_y = 7 - pixel_y;
        priority = (attr & 0x80) != 0;
    }

    // Signed mode: tile 0 at 0x9000, tiles 128-255 at 0x8800-0x8FF0
    u16 tile_addr;
    if (signed_addressing) {
        s8 signed_index = static_cast<s8>(tile_index);
        tile_addr = 0x1000 + (signed_index * 16);
    } else {
        tile_addr = tile_index * 16;
    }

    u8 color_id = GetTilePixel(tile_vram, tile_addr, pixel_x, pixel_y);
    m_line_color[screen_x] = color_id;
    m_line_priority[screen_x] = priority;

    u32 color = m_cgb_mode ? GetCGBColor(m_bg_pal_ram, palette, color_id)
                           : GetColor(m_bgp, color_id, m_display_bg);
    m_framebuffer[scanline * SCREEN_WIDTH + screen_x] = color;
}

void PPU::RenderBackground(u8 scanline) {
    u16 tile_map_base = (m_lcdc & LCDC_BG_TILE_MAP) ? 0x1C00 : 0x1800;
    bool signed_addressing = (m_lcdc & LCDC_BG_TILE_DATA) == 0;

    u8 map_y = scanline + m_scy;

    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        u8 map_x = x + m_scx;
        RenderTileLayerPixel(scanline, x, tile_map_base, signed_addressing,
                             map_x, map_y);
    }
}

void PPU::RenderWindow(u8 scanline) {
    // The window shows on lines at or below WY, with WX <= 166 (WX-7 is
    // the left edge; 167+ pushes it fully off-screen)
    if (scanline < m_wy || m_wx >= 167) {
        return;
    }

    u16 tile_map_base = (m_lcdc & LCDC_WIN_TILE_MAP) ? 0x1C00 : 0x1800;
    bool signed_addressing = (m_lcdc & LCDC_BG_TILE_DATA) == 0;

    // The row comes from the internal counter, not LY - WY, so lines
    // where the window was hidden are not skipped over in its content
    u8 window_y = m_window_line;

    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        s16 window_x = x - (m_wx - 7);
        if (window_x < 0) continue;

        RenderTileLayerPixel(scanline, x, tile_map_base, signed_addressing,
                             static_cast<u8>(window_x), window_y);
    }

    m_window_line++;
}

void PPU::RenderSprites(u8 scanline) {
    if (m_sprite_count == 0) return;

    u8* vram = m_memory.GetVRAM();
    u8 sprite_height = (m_lcdc & LCDC_OBJ_SIZE) ? 16 : 8;

    // In CGB mode LCDC bit 0 enables BG-over-OBJ priority; when clear,
    // sprites always win regardless of the priority flags
    bool master_priority = !m_cgb_mode || (m_lcdc & LCDC_BG_ENABLE) != 0;

    // Draw order is lowest priority first so the winner lands on top.
    // DMG (and CGB with OPRI set): lowest X wins, ties by OAM index.
    // CGB default: OAM index alone decides.
    std::array<u8, 10> order;
    for (u8 i = 0; i < m_sprite_count; i++) {
        order[i] = i;
    }
    bool x_priority = !m_cgb_mode || (m_opri & 0x01) != 0;
    if (x_priority) {
        std::sort(order.begin(), order.begin() + m_sprite_count,
                  [this](u8 a, u8 b) {
            if (m_sprite_buffer[a].x != m_sprite_buffer[b].x) {
                return m_sprite_buffer[a].x > m_sprite_buffer[b].x;
            }
            return a > b;
        });
    } else {
        std::reverse(order.begin(), order.begin() + m_sprite_count);
    }

    for (u8 n = 0; n < m_sprite_count; n++) {
        const Sprite& sprite = m_sprite_buffer[order[n]];

        s16 sprite_y = sprite.y - 16;
        s16 sprite_x = sprite.x - 8;

        // Calculate Y offset within sprite
        u8 y_offset = scanline - sprite_y;
        if (sprite.y_flip()) {
            y_offset = sprite_height - 1 - y_offset;
        }

        // Get tile index
        u8 tile_index = sprite.tile;
        if (sprite_height == 16) {
            tile_index &= 0xFE;  // Use even tile for 8x16 sprites
        }

        // Calculate tile address
        u16 tile_addr = tile_index * 16;

        // Select palette and tile bank
        u8 palette = sprite.palette() ? m_obp1 : m_obp0;
        const u8* tile_vram = m_cgb_mode
            ? m_memory.GetVRAMBank(sprite.vram_bank()) : vram;

        // Render sprite pixels
        for (u8 x = 0; x < 8; x++) {
            s16 screen_x = sprite_x + x;

            // Skip if off-screen
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;

            u8 pixel_x = sprite.x_flip() ? (7 - x) : x;
            u8 color_id = GetTilePixel(tile_vram, tile_addr, pixel_x, y_offset);

            // Color 0 is transparent for sprites
            if (color_id == 0) continue;

            // BG color 1-3 covers the sprite if the sprite is flagged
            // behind the BG, or (CGB) the BG tile has priority
            if (master_priority && m_line_color[screen_x] != 0) {
                bool bg_wins = !sprite.priority() ||
                               (m_cgb_mode && m_line_priority[screen_x]);
                if (bg_wins) continue;
            }

            // Draw sprite pixel
            u32 color = m_cgb_mode
                ? GetCGBColor(m_obj_pal_ram, sprite.cgb_palette(), color_id)
                : GetColor(palette, color_id,
                           sprite.palette() ? m_display_obj1 : m_display_obj0);
            m_framebuffer[scanline * SCREEN_WIDTH + screen_x] = color;
        }
    }
}

u8 PPU::GetTilePixel(const u8* vram, u16 tile_data_addr, u8 x, u8 y) {
    // Each tile is 16 bytes (8x8 pixels, 2 bits per pixel)
    // Each row is 2 bytes
    u16 addr = tile_data_addr + (y * 2);

    u8 byte1 = vram[addr];
    u8 byte2 = vram[addr + 1];

    // Get the bit for this pixel (MSB = leftmost pixel)
    u8 bit = 7 - x;
    u8 color_id = ((byte2 >> bit) & 1) << 1 | ((byte1 >> bit) & 1);

    return color_id;
}

u32 PPU::GetColor(u8 palette, u8 color_id, const std::array<u32, 4>& display) {
    // Extract 2-bit color from palette
    u8 color = (palette >> (color_id * 2)) & 0x03;

    return display[color];
}

u32 PPU::GetCGBColor(const std::array<u8, 64>& pal_ram, u8 palette, u8 color_id) {
    // Each color is little-endian RGB555 (2 bytes), 4 colors per palette
    size_t offset = palette * 8 + color_id * 2;
    u16 rgb = static_cast<u16>(pal_ram[offset]) |
              (static_cast<u16>(pal_ram[offset + 1]) << 8);

    u8 r = rgb & 0x1F;
    u8 g = (rgb >> 5) & 0x1F;
    u8 b = (rgb >> 10) & 0x1F;

    // Expand 5-bit channels to 8 bits; framebuffer byte order is R,G,B,A
    auto expand = [](u8 c) -> u32 { return (c << 3) | (c >> 2); };
    return 0xFF000000 | (expand(b) << 16) | (expand(g) << 8) | expand(r);
}

void PPU::RegisterIOHandlers() {
    // LCDC - LCD Control
    m_memory.RegisterIOHandler(0xFF40,
        [this](u16) { return m_lcdc; },
        [this](u16, u8 value) {
            bool was_enabled = (m_lcdc & LCDC_LCD_ENABLE) != 0;
            m_lcdc = value;
            bool now_enabled = (m_lcdc & LCDC_LCD_ENABLE) != 0;

            if (was_enabled && !now_enabled) {
                // LCD off: LY resets to 0 and STAT reports mode 0
                m_scanline = 0;
                m_cycle_counter = 0;
                m_mode = Mode::HBLANK;
                m_first_line = false;
                m_stat &= ~STAT_MODE_FLAG;
            } else if (!was_enabled && now_enabled) {
                // LCD on: the first scanline starts 4 dots in (LY flips
                // to 1 exactly 452 dots after the enabling write) and
                // skips OAM scan - STAT reports mode 0 until drawing
                m_scanline = 0;
                m_window_line = 0;
                m_cycle_counter = 4;
                m_mode = Mode::OAM_SCAN;
                m_first_line = true;
                m_stat &= ~STAT_MODE_FLAG;
                UpdateStatRegister();
            }
        }
    );
    
    // STAT - LCD Status
    m_memory.RegisterIOHandler(0xFF41,
        [this](u16) { return m_stat; },
        [this](u16, u8 value) { m_stat = (value & 0xF8) | (m_stat & 0x07); }
    );
    
    // SCY - Scroll Y
    m_memory.RegisterIOHandler(0xFF42,
        [this](u16) { return m_scy; },
        [this](u16, u8 value) { m_scy = value; }
    );
    
    // SCX - Scroll X
    m_memory.RegisterIOHandler(0xFF43,
        [this](u16) { return m_scx; },
        [this](u16, u8 value) { m_scx = value; }
    );
    
    // LY - LCD Y (read-only)
    m_memory.RegisterIOHandler(0xFF44,
        [this](u16) { return m_scanline; },
        [](u16, u8) { /* Read-only */ }
    );
    
    // LYC - LY Compare
    m_memory.RegisterIOHandler(0xFF45,
        [this](u16) { return m_lyc; },
        [this](u16, u8 value) { m_lyc = value; }
    );
    
    // BGP - BG Palette
    m_memory.RegisterIOHandler(0xFF47,
        [this](u16) { return m_bgp; },
        [this](u16, u8 value) { m_bgp = value; }
    );
    
    // OBP0 - OBJ Palette 0
    m_memory.RegisterIOHandler(0xFF48,
        [this](u16) { return m_obp0; },
        [this](u16, u8 value) { m_obp0 = value; }
    );
    
    // OBP1 - OBJ Palette 1
    m_memory.RegisterIOHandler(0xFF49,
        [this](u16) { return m_obp1; },
        [this](u16, u8 value) { m_obp1 = value; }
    );
    
    // WY - Window Y
    m_memory.RegisterIOHandler(0xFF4A,
        [this](u16) { return m_wy; },
        [this](u16, u8 value) { m_wy = value; }
    );
    
    // WX - Window X
    m_memory.RegisterIOHandler(0xFF4B,
        [this](u16) { return m_wx; },
        [this](u16, u8 value) { m_wx = value; }
    );

    // CGB color palette registers (0xFF68-0xFF6B): an index register with
    // auto-increment-on-write, and a data window into palette RAM
    m_memory.RegisterIOHandler(0xFF68,
        [this](u16) -> u8 { return m_cgb_mode ? (m_bcps | 0x40) : 0xFF; },
        [this](u16, u8 value) { if (m_cgb_mode) m_bcps = value & 0xBF; }
    );
    m_memory.RegisterIOHandler(0xFF69,
        [this](u16) -> u8 {
            return m_cgb_mode ? m_bg_pal_ram[m_bcps & 0x3F] : 0xFF;
        },
        [this](u16, u8 value) {
            if (!m_cgb_mode) return;
            m_bg_pal_ram[m_bcps & 0x3F] = value;
            if (m_bcps & 0x80) {
                m_bcps = 0x80 | ((m_bcps + 1) & 0x3F);
            }
        }
    );
    m_memory.RegisterIOHandler(0xFF6A,
        [this](u16) -> u8 { return m_cgb_mode ? (m_ocps | 0x40) : 0xFF; },
        [this](u16, u8 value) { if (m_cgb_mode) m_ocps = value & 0xBF; }
    );
    m_memory.RegisterIOHandler(0xFF6B,
        [this](u16) -> u8 {
            return m_cgb_mode ? m_obj_pal_ram[m_ocps & 0x3F] : 0xFF;
        },
        [this](u16, u8 value) {
            if (!m_cgb_mode) return;
            m_obj_pal_ram[m_ocps & 0x3F] = value;
            if (m_ocps & 0x80) {
                m_ocps = 0x80 | ((m_ocps + 1) & 0x3F);
            }
        }
    );

    // OPRI - CGB object priority mode (0xFF6C): bit 0 set selects
    // DMG-style X-coordinate priority (the boot ROM sets it for DMG carts)
    m_memory.RegisterIOHandler(0xFF6C,
        [this](u16) -> u8 { return m_cgb_mode ? (0xFE | (m_opri & 0x01)) : 0xFF; },
        [this](u16, u8 value) { if (m_cgb_mode) m_opri = value & 0x01; }
    );
}

void PPU::SaveState(StateBuffer& state) const {
    state.Write(m_mode);
    state.Write(m_cycle_counter);
    state.Write(m_scanline);
    state.Write(m_frame_ready);
    state.Write(m_first_line);
    state.Write(m_sprite_count);
    state.WriteBytes(m_sprite_buffer.data(), sizeof(Sprite) * m_sprite_buffer.size());
    state.Write(m_lcdc);
    state.Write(m_stat);
    state.Write(m_scy);
    state.Write(m_scx);
    state.Write(m_lyc);
    state.Write(m_bgp);
    state.Write(m_obp0);
    state.Write(m_obp1);
    state.Write(m_wy);
    state.Write(m_wx);
    state.Write(m_window_line);
    state.Write(m_opri);
    state.Write(m_bcps);
    state.Write(m_ocps);
    state.WriteBytes(m_bg_pal_ram.data(), m_bg_pal_ram.size());
    state.WriteBytes(m_obj_pal_ram.data(), m_obj_pal_ram.size());
    state.WriteBytes(m_framebuffer.data(), m_framebuffer.size() * sizeof(u32));
}

bool PPU::LoadState(StateBuffer& state) {
    return state.Read(m_mode) &&
           state.Read(m_cycle_counter) &&
           state.Read(m_scanline) &&
           state.Read(m_frame_ready) &&
           state.Read(m_first_line) &&
           state.Read(m_sprite_count) &&
           state.ReadBytes(m_sprite_buffer.data(), sizeof(Sprite) * m_sprite_buffer.size()) &&
           state.Read(m_lcdc) &&
           state.Read(m_stat) &&
           state.Read(m_scy) &&
           state.Read(m_scx) &&
           state.Read(m_lyc) &&
           state.Read(m_bgp) &&
           state.Read(m_obp0) &&
           state.Read(m_obp1) &&
           state.Read(m_wy) &&
           state.Read(m_wx) &&
           state.Read(m_window_line) &&
           state.Read(m_opri) &&
           state.Read(m_bcps) &&
           state.Read(m_ocps) &&
           state.ReadBytes(m_bg_pal_ram.data(), m_bg_pal_ram.size()) &&
           state.ReadBytes(m_obj_pal_ram.data(), m_obj_pal_ram.size()) &&
           state.ReadBytes(m_framebuffer.data(), m_framebuffer.size() * sizeof(u32));
}
