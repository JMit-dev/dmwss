#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include "../scheduler/scheduler.hpp"
#include <array>
#include <functional>

class StateBuffer;

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
        u8 cgb_palette() const { return flags & 0x07; }        // CGB: OBJ palette 0-7
        u8 vram_bank() const { return (flags >> 3) & 1; }      // CGB: tile VRAM bank
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

    // Save states
    void SaveState(StateBuffer& state) const;
    bool LoadState(StateBuffer& state);

    // Display palette: the four RGBA colors DMG shades 0-3 map to
    // (0 = lightest, 3 = darkest). Cosmetic only, not serialized.
    void SetDisplayPalette(const std::array<u32, 4>& colors) { m_display_palette = colors; }

    // CGB mode enables color palette RAM, tile attributes, and the
    // LCDC bit 0 master-priority interpretation
    void SetCGBMode(bool enabled) { m_cgb_mode = enabled; }

    // Called at the start of every HBlank of a visible scanline
    // (used by CGB HBlank DMA)
    using HBlankCallback = std::function<void()>;
    void SetHBlankCallback(HBlankCallback callback) { m_hblank_callback = std::move(callback); }

    // DMG OAM corruption bug: called by the CPU when an instruction
    // performs a 16-bit increment/decrement (or stack access) with a
    // value in the 0xFE00-0xFEFF range. Corrupts OAM if the PPU is in
    // mode 2 (see Pan Docs "OAM Corruption Bug").
    void TriggerOAMBug(OAMBugType type);

private:
    Memory& m_memory;
    Scheduler& m_scheduler;

    // PPU state
    Mode m_mode;
    u32 m_cycle_counter;
    u8 m_scanline;          // LY register (0-153)
    bool m_frame_ready;
    bool m_first_line;      // First scanline after LCD enable skips OAM scan

    // Framebuffer (160x144 pixels, RGBA format)
    std::array<u32, SCREEN_WIDTH * SCREEN_HEIGHT> m_framebuffer;

    // RGBA colors for DMG shades 0-3 (byte order R,G,B,A => 0xAABBGGRR)
    std::array<u32, 4> m_display_palette = {
        0xFFFFFFFF, 0xFFAAAAAA, 0xFF555555, 0xFF000000
    };

    // CGB state
    bool m_cgb_mode = false;
    u8 m_bcps = 0;                          // BG palette index (0xFF68)
    u8 m_ocps = 0;                          // OBJ palette index (0xFF6A)
    std::array<u8, 64> m_bg_pal_ram{};      // 8 palettes x 4 colors x RGB555
    std::array<u8, 64> m_obj_pal_ram{};
    HBlankCallback m_hblank_callback;

    // Per-scanline BG/window color indices and CGB BG-priority flags,
    // used to resolve sprite-vs-background priority
    std::array<u8, SCREEN_WIDTH> m_line_color{};
    std::array<bool, SCREEN_WIDTH> m_line_priority{};

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

    // OAM corruption bug helpers (row = 8-byte OAM row, 1-19)
    void CorruptOAMRead(u32 row);
    void CorruptOAMWrite(u32 row);
    void CorruptOAMIncDec(u32 row);

    // Tile/pixel helpers
    u8 GetTilePixel(const u8* vram, u16 tile_data_addr, u8 x, u8 y);
    u32 GetColor(u8 palette, u8 color_id);
    u32 GetCGBColor(const std::array<u8, 64>& pal_ram, u8 palette, u8 color_id);

    // Shared BG/window tile row renderer (map_y/map_x in tilemap pixels)
    void RenderTileLayerPixel(u8 scanline, u8 screen_x, u16 tile_map_base,
                              bool signed_addressing, u8 map_x, u8 map_y);

    // I/O register handlers
    void RegisterIOHandlers();
    u8 ReadLCDC();
    u8 ReadSTAT();
    u8 ReadLY();
    void WriteLCDC(u8 value);
    void WriteSTAT(u8 value);
    void WriteLYC(u8 value);
};
