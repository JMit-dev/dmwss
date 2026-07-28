#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include <functional>

class StateBuffer;

// Serial data transfer (link cable): SB (0xFF01) holds the byte being
// shifted, SC (0xFF02) controls the transfer. Bit 7 of SC starts a
// transfer, bit 0 selects the clock source (1 = internal 8192 Hz,
// 0 = external, i.e. the other Game Boy drives the clock).
//
// A transfer exchanges bytes bit-by-bit between the two consoles; after
// 8 bits SB holds the peer's byte, SC bit 7 clears, and the serial
// interrupt fires. With no cable attached an internally-clocked transfer
// receives 0xFF (the line reads high) and an externally-clocked transfer
// never completes.
class Serial {
public:
    // Serial interrupt bit in IF/IE
    static constexpr u8 INTERRUPT_BIT = 0x08;

    // Internal clock is 8192 Hz => 512 T-cycles per bit, 4096 per byte
    static constexpr u32 CYCLES_PER_TRANSFER = 4096;

    // Link cable: invoked when an internally-clocked transfer completes,
    // exchanging our byte for the peer's. Returns the received byte
    // (0xFF when nothing is connected).
    using LinkCallback = std::function<u8(u8 sent)>;

    explicit Serial(Memory& memory);

    void Reset();
    void Step(u32 cycles);

    void SetLinkCallback(LinkCallback callback) { m_link = std::move(callback); }

    // Called by the link layer when the peer (using its internal clock)
    // shifts a byte into us. Returns the byte we send back. Completes our
    // pending externally-clocked transfer, if any.
    u8 ExchangeAsSlave(u8 incoming);

    // Save states
    void SaveState(StateBuffer& state) const;
    bool LoadState(StateBuffer& state);

private:
    Memory& m_memory;
    LinkCallback m_link;

    u8 m_sb;                  // Serial transfer data (0xFF01)
    u8 m_sc;                  // Serial transfer control (0xFF02)
    bool m_transfer_active;   // Internally-clocked transfer in progress
    u32 m_cycle_counter;

    void RegisterIOHandlers();
};
