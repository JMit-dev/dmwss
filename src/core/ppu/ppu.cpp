#include "ppu.hpp"
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
    m_frame_ready = false;
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
    
    // Render layers
    if (m_lcdc & LCDC_BG_ENABLE) {
        RenderBackground(m_scanline);
    }
    
    if (m_lcdc & LCDC_WIN_ENABLE) {
        RenderWindow(m_scanline);
    }
    
    if (m_lcdc & LCDC_OBJ_ENABLE) {
        RenderSprites(m_scanline);
    }
}

void PPU::RenderBackground(u8 scanline) {
    u8* vram = m_memory.GetVRAM();
    
    // Determine tile map address
    u16 tile_map_base = (m_lcdc & LCDC_BG_TILE_MAP) ? 0x1C00 : 0x1800;
    
    // Determine tile data addressing mode
    bool signed_addressing = (m_lcdc & LCDC_BG_TILE_DATA) == 0;
    u16 tile_data_base = signed_addressing ? 0x1000 : 0x0000;
    
    // Calculate Y position with scroll
    u8 y = scanline + m_scy;
    u8 tile_y = y / 8;
    u8 pixel_y = y % 8;
    
    // Render 160 pixels
    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        // Calculate X position with scroll
        u8 scroll_x = x + m_scx;
        u8 tile_x = scroll_x / 8;
        u8 pixel_x = scroll_x % 8;
        
        // Get tile index from tile map
        u16 tile_map_addr = tile_map_base + (tile_y * 32) + tile_x;
        u8 tile_index = vram[tile_map_addr];
        
        // Calculate tile data address
        u16 tile_addr;
        if (signed_addressing) {
            s8 signed_index = static_cast<s8>(tile_index);
            tile_addr = tile_data_base + ((signed_index + 128) * 16);
        } else {
            tile_addr = tile_data_base + (tile_index * 16);
        }
        
        // Get pixel color (2 bits per pixel)
        u8 color_id = GetTilePixel(tile_addr, pixel_x, pixel_y);
        
        // Apply palette and set pixel
        u32 color = GetColor(m_bgp, color_id);
        m_framebuffer[scanline * SCREEN_WIDTH + x] = color;
    }
}

void PPU::RenderWindow(u8 scanline) {
    // Check if window is visible on this scanline
    if (scanline < m_wy) {
        return;
    }
    
    u8* vram = m_memory.GetVRAM();
    
    // Determine tile map address
    u16 tile_map_base = (m_lcdc & LCDC_WIN_TILE_MAP) ? 0x1C00 : 0x1800;
    
    // Determine tile data addressing mode
    bool signed_addressing = (m_lcdc & LCDC_BG_TILE_DATA) == 0;
    u16 tile_data_base = signed_addressing ? 0x1000 : 0x0000;
    
    // Window Y coordinate
    u8 window_y = scanline - m_wy;
    u8 tile_y = window_y / 8;
    u8 pixel_y = window_y % 8;
    
    // Render window pixels
    for (u8 x = 0; x < SCREEN_WIDTH; x++) {
        // Check if pixel is in window area
        s16 window_x = x - (m_wx - 7);
        if (window_x < 0) continue;
        
        u8 tile_x = window_x / 8;
        u8 pixel_x = window_x % 8;
        
        // Get tile index from tile map
        u16 tile_map_addr = tile_map_base + (tile_y * 32) + tile_x;
        u8 tile_index = vram[tile_map_addr];
        
        // Calculate tile data address
        u16 tile_addr;
        if (signed_addressing) {
            s8 signed_index = static_cast<s8>(tile_index);
            tile_addr = tile_data_base + ((signed_index + 128) * 16);
        } else {
            tile_addr = tile_data_base + (tile_index * 16);
        }
        
        // Get pixel color
        u8 color_id = GetTilePixel(tile_addr, pixel_x, pixel_y);
        
        // Apply palette and set pixel
        u32 color = GetColor(m_bgp, color_id);
        m_framebuffer[scanline * SCREEN_WIDTH + x] = color;
    }
}

void PPU::RenderSprites(u8 scanline) {
    if (m_sprite_count == 0) return;
    
    u8* vram = m_memory.GetVRAM();
    u8 sprite_height = (m_lcdc & LCDC_OBJ_SIZE) ? 16 : 8;
    
    // Render sprites (in reverse order for priority)
    for (s8 i = m_sprite_count - 1; i >= 0; i--) {
        const Sprite& sprite = m_sprite_buffer[i];
        
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
        
        // Select palette
        u8 palette = sprite.palette() ? m_obp1 : m_obp0;
        
        // Render sprite pixels
        for (u8 x = 0; x < 8; x++) {
            s16 screen_x = sprite_x + x;
            
            // Skip if off-screen
            if (screen_x < 0 || screen_x >= SCREEN_WIDTH) continue;
            
            u8 pixel_x = sprite.x_flip() ? (7 - x) : x;
            u8 color_id = GetTilePixel(tile_addr, pixel_x, y_offset);
            
            // Color 0 is transparent for sprites
            if (color_id == 0) continue;
            
            // Check priority (if sprite is behind BG)
            if (!sprite.priority()) {
                // Get BG color at this position
                u32 bg_pixel = m_framebuffer[scanline * SCREEN_WIDTH + screen_x];
                // If BG pixel is not color 0 (white), skip sprite pixel
                if (bg_pixel != GetColor(m_bgp, 0)) {
                    continue;
                }
            }
            
            // Draw sprite pixel
            u32 color = GetColor(palette, color_id);
            m_framebuffer[scanline * SCREEN_WIDTH + screen_x] = color;
        }
    }
}

u8 PPU::GetTilePixel(u16 tile_data_addr, u8 x, u8 y) {
    u8* vram = m_memory.GetVRAM();
    
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

u32 PPU::GetColor(u8 palette, u8 color_id) {
    // Extract 2-bit color from palette
    u8 color = (palette >> (color_id * 2)) & 0x03;
    
    // Map to grayscale (DMG palette)
    // 0 = White, 1 = Light gray, 2 = Dark gray, 3 = Black
    static const u32 colors[4] = {
        0xFFFFFFFF,  // White
        0xFFAAAAAA,  // Light gray
        0xFF555555,  // Dark gray
        0xFF000000   // Black
    };
    
    return colors[color];
}

void PPU::RegisterIOHandlers() {
    // LCDC - LCD Control
    m_memory.RegisterIOHandler(0xFF40,
        [this](u16) { return m_lcdc; },
        [this](u16, u8 value) { m_lcdc = value; }
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
}
