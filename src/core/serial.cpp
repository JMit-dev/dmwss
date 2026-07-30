#include "serial.hpp"
#include "../machine/savestate.hpp"
#include <spdlog/spdlog.h>

Serial::Serial(Memory& memory)
    : m_memory(memory) {
    Reset();
    RegisterIOHandlers();
}

void Serial::Reset() {
    m_sb = 0x00;
    m_sc = 0x00;
    m_transfer_active = false;
    m_cycle_counter = 0;
}

void Serial::RegisterIOHandlers() {
    // SB - Serial transfer data
    m_memory.RegisterIOHandler(0xFF01,
        [this](u16) -> u8 { return m_sb; },
        [this](u16, u8 value) { m_sb = value; });

    // SC - Serial transfer control: only bits 7 and 0 exist on DMG,
    // the rest read as 1
    m_memory.RegisterIOHandler(0xFF02,
        [this](u16) -> u8 { return m_sc | 0x7E; },
        [this](u16, u8 value) {
            m_sc = value & 0x81;
            if ((m_sc & 0x80) && (m_sc & 0x01)) {
                // Internal clock: we drive the transfer
                m_transfer_active = true;
                m_cycle_counter = 0;
            }
            // External clock: nothing to do locally; the peer's clock
            // (ExchangeAsSlave) completes the transfer, or it hangs
            // forever without a cable, matching hardware
        });
}

void Serial::Step(u32 cycles) {
    if (!m_transfer_active) {
        return;
    }

    m_cycle_counter += cycles;
    if (m_cycle_counter < CYCLES_PER_TRANSFER) {
        return;
    }

    // Transfer complete: exchange with the peer (a disconnected line
    // reads high, so every received bit is 1)
    u8 received = m_link ? m_link(m_sb) : 0xFF;
    m_sb = received;
    m_sc &= 0x7F;
    m_transfer_active = false;
    m_cycle_counter = 0;
    m_memory.RequestInterrupt(INTERRUPT_BIT);
}

u8 Serial::ExchangeAsSlave(u8 incoming) {
    // The shift register always shifts on an external clock; the transfer
    // only "completes" (bit 7 clears + interrupt) if one was started
    u8 outgoing = m_sb;
    m_sb = incoming;
    if ((m_sc & 0x80) && !(m_sc & 0x01)) {
        m_sc &= 0x7F;
        m_memory.RequestInterrupt(INTERRUPT_BIT);
    }
    return outgoing;
}

void Serial::SaveState(StateBuffer& state) const {
    state.Write(m_sb);
    state.Write(m_sc);
    state.Write(m_transfer_active);
    state.Write(m_cycle_counter);
}

bool Serial::LoadState(StateBuffer& state) {
    return state.Read(m_sb) &&
           state.Read(m_sc) &&
           state.Read(m_transfer_active) &&
           state.Read(m_cycle_counter);
}
