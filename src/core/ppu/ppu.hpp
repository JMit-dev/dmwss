#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include "../scheduler/scheduler.hpp"
#include <array>

class PPU {
public:
    // LCD dimensions
    static constexpr u32 SCREEN_WIDTH = 160;
    static constexpr u32 SCREEN_HEIGHT = 144;

    // PPU modes
    enum class Mode : u8 {
        HBLANK = 0,      // Horizontal blank - 204 cycles
        VBLANK = 1,      // Vertical blank - 4560 cycles (10 lines)
        OAM_SCAN = 2,    // OAM search - 80 cycles
        DRAWING = 3      // Pixel transfer - 172 cycles
    };

    // PPU timing constants (in cycles)
    static constexpr u32 CYCLES_PER_SCANLINE = 456;  // Total cycles per scanline
    static constexpr u32 OAM_SCAN_CYCLES = 80;
    static constexpr u32 DRAWING_CYCLES = 172;
    static constexpr u32 HBLANK_CYCLES = 204;
    static constexpr u32 VBLANK_LINES = 10;
    static constexpr u32 SCANLINES_PER_FRAME = 154;  // 144 visible + 10 vblank

    // LCD Control Register (LCDC) flags
    enum LCDCFlags : u8 {
        LCDC_BG_ENABLE        = 0x01,  // Bit 0: BG/Window display
        LCDC_OBJ_ENABLE       = 0x02,  // Bit 1: OBJ (sprite) display
        LCDC_OBJ_SIZE         = 0x04,  // Bit 2: OBJ size (0=8x8, 1=8x16)
        LCDC_BG_TILE_MAP      = 0x08,  // Bit 3: BG tile map (0=9800-9BFF, 1=9C00-9FFF)
        LCDC_BG_TILE_DATA     = 0x10,  // Bit 4: BG/Win tile data (0=8800-97FF, 1=8000-8FFF)
        LCDC_WIN_ENABLE       = 0x20,  // Bit 5: Window display
        LCDC_WIN_TILE_MAP     = 0x40,  // Bit 6: Window tile map (0=9800-9BFF, 1=9C00-9FFF)
        LCDC_LCD_ENABLE       = 0x80   // Bit 7: LCD enable
    };

    // LCD Status Register (STAT) flags
    enum STATFlags : u8 {
        STAT_MODE_FLAG        = 0x03,  // Bits 0-1: Mode flag
        STAT_LYC_EQUAL        = 0x04,  // Bit 2: LYC=LY flag
        STAT_HBLANK_INT       = 0x08,  // Bit 3: Mode 0 interrupt
        STAT_VBLANK_INT       = 0x10,  // Bit 4: Mode 1 interrupt
        STAT_OAM_INT          = 0x20,  // Bit 5: Mode 2 interrupt
        STAT_LYC_INT          = 0x40   // Bit 6: LYC=LY interrupt
    };

    // Sprite attributes
    struct Sprite {
        u8 y;          // Y position + 16
        u8 x;          // X position + 8
        u8 tile;       // Tile index
        u8 flags;      // Attributes

        // Attribute flags
        bool priority() const { return (flags & 0x80) == 0; }  // 0=above BG, 1=behind BG
        bool y_flip() const { return (flags & 0x40) != 0; }
        bool x_flip() const { return (flags & 0x20) != 0; }
        u8 palette() const { return (flags & 0x10) ? 1 : 0; }  // DMG: 0=OBP0, 1=OBP1
    };

    PPU(Memory& memory, Scheduler& scheduler);
    ~PPU() = default;

    // Step PPU by given number of cycles
    void Step(u32 cycles);

    // Reset PPU to power-on state
    void Reset();

    // Get framebuffer (RGBA format)
    const u32* GetFramebuffer() const { return m_framebuffer.data(); }

    // Check if frame is ready
    bool IsFrameReady() const { return m_frame_ready; }
    void ClearFrameReady() { m_frame_ready = false; }

private:
    Memory& m_memory;
    Scheduler& m_scheduler;

    // PPU state
    Mode m_mode;
    u32 m_cycle_counter;
    u8 m_scanline;          // LY register (0-153)
    bool m_frame_ready;

    // Framebuffer (160x144 pixels, RGBA format)
    std::array<u32, SCREEN_WIDTH * SCREEN_HEIGHT> m_framebuffer;

    // Sprite buffer for current scanline
    std::array<Sprite, 10> m_sprite_buffer;  // Max 10 sprites per line
    u8 m_sprite_count;

    // LCD registers (these are memory-mapped, but we cache them for performance)
    u8 m_lcdc;   // LCD Control (0xFF40)
    u8 m_stat;   // LCD Status (0xFF41)
    u8 m_scy;    // Scroll Y (0xFF42)
    u8 m_scx;    // Scroll X (0xFF43)
    u8 m_lyc;    // LY Compare (0xFF45)
    u8 m_bgp;    // BG Palette (0xFF47)
    u8 m_obp0;   // OBJ Palette 0 (0xFF48)
    u8 m_obp1;   // OBJ Palette 1 (0xFF49)
    u8 m_wy;     // Window Y (0xFF4A)
    u8 m_wx;     // Window X (0xFF4B)

    // Mode switching
    void SetMode(Mode mode);
    void UpdateStatRegister();

    // Rendering
    void RenderScanline();
    void RenderBackground(u8 scanline);
    void RenderWindow(u8 scanline);
    void RenderSprites(u8 scanline);

    // OAM scan
    void ScanOAM();

    // Tile/pixel helpers
    u8 GetTilePixel(u16 tile_data_addr, u8 x, u8 y);
    u32 GetColor(u8 palette, u8 color_id);

    // I/O register handlers
    void RegisterIOHandlers();
    u8 ReadLCDC();
    u8 ReadSTAT();
    u8 ReadLY();
    void WriteLCDC(u8 value);
    void WriteSTAT(u8 value);
    void WriteLYC(u8 value);
};
