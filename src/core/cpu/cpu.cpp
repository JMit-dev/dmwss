#include "cpu.hpp"
#include <spdlog/spdlog.h>

CPU::CPU(Memory& memory, Scheduler& scheduler)
    : m_memory(memory)
    , m_scheduler(scheduler)
    , m_ime(false)
    , m_halted(false)
    , m_stopped(false)
    , m_cycles(0) {
    Reset();
}

void CPU::Reset() {
    // Initial register values after boot ROM
    m_regs.af = 0x01B0;
    m_regs.bc = 0x0013;
    m_regs.de = 0x00D8;
    m_regs.hl = 0x014D;
    m_regs.sp = 0xFFFE;
    m_regs.pc = 0x0100;  // Start of cartridge

    m_ime = false;
    m_halted = false;
    m_stopped = false;
    m_cycles = 0;

    spdlog::debug("CPU reset");
}

u32 CPU::Step() {
    m_cycles = 0;

    // Check if we should wake from HALT
    // HALT wakes up when any interrupt is pending (IE & IF != 0), regardless of IME
    if (m_halted) {
        u8 if_reg = m_memory.Read(0xFF0F);
        u8 ie_reg = m_memory.Read(0xFFFF);
        if ((if_reg & ie_reg) != 0) {
            m_halted = false;
            spdlog::trace("Waking from HALT, IF={:02X} IE={:02X}", if_reg, ie_reg);
        } else {
            // Still halted, consume cycles and return
            m_cycles = 4;
            return m_cycles;
        }
    }

    // Handle interrupts (only if IME is set)
    ServiceInterrupts();

    // Fetch and execute
    u8 opcode = FetchByte();
    ExecuteInstruction(opcode);

    return m_cycles;
}

void CPU::RequestInterrupt(u8 interrupt_bit) {
    u8 if_reg = m_memory.Read(0xFF0F);
    m_memory.Write(0xFF0F, if_reg | (1 << interrupt_bit));

    // Wake from HALT
    if (m_halted) {
        m_halted = false;
    }
}

void CPU::ServiceInterrupts() {
    if (!m_ime) return;

    u8 if_reg = m_memory.Read(0xFF0F);  // Interrupt Flag
    u8 ie_reg = m_memory.Read(0xFFFF);  // Interrupt Enable

    u8 triggered = if_reg & ie_reg;
    if (triggered == 0) return;

    // Find highest priority interrupt (lowest bit)
    for (u8 i = 0; i < 5; i++) {
        if (triggered & (1 << i)) {
            // Disable IME
            m_ime = false;

            // Clear the interrupt flag
            m_memory.Write(0xFF0F, if_reg & ~(1 << i));

            // Push PC to stack
            Push(m_regs.pc);

            // Jump to interrupt vector
            u16 vector = 0x0040 + (i * 0x08);
            m_regs.pc = vector;

            m_cycles += 20;  // Interrupt servicing takes 5 M-cycles

            spdlog::trace("Servicing interrupt {}, jumping to 0x{:04X}", i, vector);
            break;
        }
    }
}

void CPU::ExecuteInstruction(u8 opcode) {
    switch (opcode) {
        // NOP
        case 0x00: OP_NOP(); break;

        // 16-bit loads
        case 0x01: OP_LD_rr_nn(m_regs.bc); break;
        case 0x11: OP_LD_rr_nn(m_regs.de); break;
        case 0x21: OP_LD_rr_nn(m_regs.hl); break;
        case 0x31: OP_LD_rr_nn(m_regs.sp); break;

        case 0x08: OP_LD_nn_SP(); break;
        case 0xF8: OP_LD_HL_SP_e(); break;
        case 0xF9: OP_LD_SP_HL(); break;

        // 8-bit loads - LD r,n
        case 0x06: OP_LD_r_n(m_regs.b); break;
        case 0x0E: OP_LD_r_n(m_regs.c); break;
        case 0x16: OP_LD_r_n(m_regs.d); break;
        case 0x1E: OP_LD_r_n(m_regs.e); break;
        case 0x26: OP_LD_r_n(m_regs.h); break;
        case 0x2E: OP_LD_r_n(m_regs.l); break;
        case 0x3E: OP_LD_r_n(m_regs.a); break;

        // LD r,r' (B destination)
        case 0x40: OP_LD_r_r(m_regs.b, m_regs.b); break;
        case 0x41: OP_LD_r_r(m_regs.b, m_regs.c); break;
        case 0x42: OP_LD_r_r(m_regs.b, m_regs.d); break;
        case 0x43: OP_LD_r_r(m_regs.b, m_regs.e); break;
        case 0x44: OP_LD_r_r(m_regs.b, m_regs.h); break;
        case 0x45: OP_LD_r_r(m_regs.b, m_regs.l); break;
        case 0x46: OP_LD_r_HL(m_regs.b); break;
        case 0x47: OP_LD_r_r(m_regs.b, m_regs.a); break;

        // LD r,r' (C destination)
        case 0x48: OP_LD_r_r(m_regs.c, m_regs.b); break;
        case 0x49: OP_LD_r_r(m_regs.c, m_regs.c); break;
        case 0x4A: OP_LD_r_r(m_regs.c, m_regs.d); break;
        case 0x4B: OP_LD_r_r(m_regs.c, m_regs.e); break;
        case 0x4C: OP_LD_r_r(m_regs.c, m_regs.h); break;
        case 0x4D: OP_LD_r_r(m_regs.c, m_regs.l); break;
        case 0x4E: OP_LD_r_HL(m_regs.c); break;
        case 0x4F: OP_LD_r_r(m_regs.c, m_regs.a); break;

        // LD r,r' (D destination)
        case 0x50: OP_LD_r_r(m_regs.d, m_regs.b); break;
        case 0x51: OP_LD_r_r(m_regs.d, m_regs.c); break;
        case 0x52: OP_LD_r_r(m_regs.d, m_regs.d); break;
        case 0x53: OP_LD_r_r(m_regs.d, m_regs.e); break;
        case 0x54: OP_LD_r_r(m_regs.d, m_regs.h); break;
        case 0x55: OP_LD_r_r(m_regs.d, m_regs.l); break;
        case 0x56: OP_LD_r_HL(m_regs.d); break;
        case 0x57: OP_LD_r_r(m_regs.d, m_regs.a); break;

        // LD r,r' (E destination)
        case 0x58: OP_LD_r_r(m_regs.e, m_regs.b); break;
        case 0x59: OP_LD_r_r(m_regs.e, m_regs.c); break;
        case 0x5A: OP_LD_r_r(m_regs.e, m_regs.d); break;
        case 0x5B: OP_LD_r_r(m_regs.e, m_regs.e); break;
        case 0x5C: OP_LD_r_r(m_regs.e, m_regs.h); break;
        case 0x5D: OP_LD_r_r(m_regs.e, m_regs.l); break;
        case 0x5E: OP_LD_r_HL(m_regs.e); break;
        case 0x5F: OP_LD_r_r(m_regs.e, m_regs.a); break;

        // LD r,r' (H destination)
        case 0x60: OP_LD_r_r(m_regs.h, m_regs.b); break;
        case 0x61: OP_LD_r_r(m_regs.h, m_regs.c); break;
        case 0x62: OP_LD_r_r(m_regs.h, m_regs.d); break;
        case 0x63: OP_LD_r_r(m_regs.h, m_regs.e); break;
        case 0x64: OP_LD_r_r(m_regs.h, m_regs.h); break;
        case 0x65: OP_LD_r_r(m_regs.h, m_regs.l); break;
        case 0x66: OP_LD_r_HL(m_regs.h); break;
        case 0x67: OP_LD_r_r(m_regs.h, m_regs.a); break;

        // LD r,r' (L destination)
        case 0x68: OP_LD_r_r(m_regs.l, m_regs.b); break;
        case 0x69: OP_LD_r_r(m_regs.l, m_regs.c); break;
        case 0x6A: OP_LD_r_r(m_regs.l, m_regs.d); break;
        case 0x6B: OP_LD_r_r(m_regs.l, m_regs.e); break;
        case 0x6C: OP_LD_r_r(m_regs.l, m_regs.h); break;
        case 0x6D: OP_LD_r_r(m_regs.l, m_regs.l); break;
        case 0x6E: OP_LD_r_HL(m_regs.l); break;
        case 0x6F: OP_LD_r_r(m_regs.l, m_regs.a); break;

        // LD (HL),r
        case 0x70: OP_LD_HL_r(m_regs.b); break;
        case 0x71: OP_LD_HL_r(m_regs.c); break;
        case 0x72: OP_LD_HL_r(m_regs.d); break;
        case 0x73: OP_LD_HL_r(m_regs.e); break;
        case 0x74: OP_LD_HL_r(m_regs.h); break;
        case 0x75: OP_LD_HL_r(m_regs.l); break;
        case 0x36: OP_LD_HL_n(); break;
        case 0x77: OP_LD_HL_r(m_regs.a); break;

        // LD r,r' (A destination)
        case 0x78: OP_LD_r_r(m_regs.a, m_regs.b); break;
        case 0x79: OP_LD_r_r(m_regs.a, m_regs.c); break;
        case 0x7A: OP_LD_r_r(m_regs.a, m_regs.d); break;
        case 0x7B: OP_LD_r_r(m_regs.a, m_regs.e); break;
        case 0x7C: OP_LD_r_r(m_regs.a, m_regs.h); break;
        case 0x7D: OP_LD_r_r(m_regs.a, m_regs.l); break;
        case 0x7E: OP_LD_r_HL(m_regs.a); break;
        case 0x7F: OP_LD_r_r(m_regs.a, m_regs.a); break;

        // Special loads
        case 0x0A: OP_LD_A_BC(); break;
        case 0x1A: OP_LD_A_DE(); break;
        case 0xFA: OP_LD_A_nn(); break;
        case 0x02: OP_LD_BC_A(); break;
        case 0x12: OP_LD_DE_A(); break;
        case 0xEA: OP_LD_nn_A(); break;
        case 0xF0: OP_LDH_A_n(); break;
        case 0xE0: OP_LDH_n_A(); break;
        case 0xF2: OP_LD_A_C(); break;
        case 0xE2: OP_LD_C_A(); break;
        case 0x22: OP_LDI_HL_A(); break;
        case 0x2A: OP_LDI_A_HL(); break;
        case 0x32: OP_LDD_HL_A(); break;
        case 0x3A: OP_LDD_A_HL(); break;

        // Stack operations
        case 0xC5: OP_PUSH(m_regs.bc); break;
        case 0xD5: OP_PUSH(m_regs.de); break;
        case 0xE5: OP_PUSH(m_regs.hl); break;
        case 0xF5: OP_PUSH(m_regs.af); break;
        case 0xC1: OP_POP(m_regs.bc); break;
        case 0xD1: OP_POP(m_regs.de); break;
        case 0xE1: OP_POP(m_regs.hl); break;
        case 0xF1: OP_POP(m_regs.af); break;

        // 8-bit arithmetic - ADD
        case 0x80: OP_ADD_A_r(m_regs.b); break;
        case 0x81: OP_ADD_A_r(m_regs.c); break;
        case 0x82: OP_ADD_A_r(m_regs.d); break;
        case 0x83: OP_ADD_A_r(m_regs.e); break;
        case 0x84: OP_ADD_A_r(m_regs.h); break;
        case 0x85: OP_ADD_A_r(m_regs.l); break;
        case 0x86: OP_ADD_A_r(ReadByte(m_regs.hl)); break;
        case 0x87: OP_ADD_A_r(m_regs.a); break;
        case 0xC6: OP_ADD_A_r(FetchByte()); break;

        // ADC
        case 0x88: OP_ADC_A_r(m_regs.b); break;
        case 0x89: OP_ADC_A_r(m_regs.c); break;
        case 0x8A: OP_ADC_A_r(m_regs.d); break;
        case 0x8B: OP_ADC_A_r(m_regs.e); break;
        case 0x8C: OP_ADC_A_r(m_regs.h); break;
        case 0x8D: OP_ADC_A_r(m_regs.l); break;
        case 0x8E: OP_ADC_A_r(ReadByte(m_regs.hl)); break;
        case 0x8F: OP_ADC_A_r(m_regs.a); break;
        case 0xCE: OP_ADC_A_r(FetchByte()); break;

        // SUB
        case 0x90: OP_SUB_r(m_regs.b); break;
        case 0x91: OP_SUB_r(m_regs.c); break;
        case 0x92: OP_SUB_r(m_regs.d); break;
        case 0x93: OP_SUB_r(m_regs.e); break;
        case 0x94: OP_SUB_r(m_regs.h); break;
        case 0x95: OP_SUB_r(m_regs.l); break;
        case 0x96: OP_SUB_r(ReadByte(m_regs.hl)); break;
        case 0x97: OP_SUB_r(m_regs.a); break;
        case 0xD6: OP_SUB_r(FetchByte()); break;

        // SBC
        case 0x98: OP_SBC_A_r(m_regs.b); break;
        case 0x99: OP_SBC_A_r(m_regs.c); break;
        case 0x9A: OP_SBC_A_r(m_regs.d); break;
        case 0x9B: OP_SBC_A_r(m_regs.e); break;
        case 0x9C: OP_SBC_A_r(m_regs.h); break;
        case 0x9D: OP_SBC_A_r(m_regs.l); break;
        case 0x9E: OP_SBC_A_r(ReadByte(m_regs.hl)); break;
        case 0x9F: OP_SBC_A_r(m_regs.a); break;
        case 0xDE: OP_SBC_A_r(FetchByte()); break;

        // AND
        case 0xA0: OP_AND_r(m_regs.b); break;
        case 0xA1: OP_AND_r(m_regs.c); break;
        case 0xA2: OP_AND_r(m_regs.d); break;
        case 0xA3: OP_AND_r(m_regs.e); break;
        case 0xA4: OP_AND_r(m_regs.h); break;
        case 0xA5: OP_AND_r(m_regs.l); break;
        case 0xA6: OP_AND_r(ReadByte(m_regs.hl)); break;
        case 0xA7: OP_AND_r(m_regs.a); break;
        case 0xE6: OP_AND_r(FetchByte()); break;

        // XOR
        case 0xA8: OP_XOR_r(m_regs.b); break;
        case 0xA9: OP_XOR_r(m_regs.c); break;
        case 0xAA: OP_XOR_r(m_regs.d); break;
        case 0xAB: OP_XOR_r(m_regs.e); break;
        case 0xAC: OP_XOR_r(m_regs.h); break;
        case 0xAD: OP_XOR_r(m_regs.l); break;
        case 0xAE: OP_XOR_r(ReadByte(m_regs.hl)); break;
        case 0xAF: OP_XOR_r(m_regs.a); break;
        case 0xEE: OP_XOR_r(FetchByte()); break;

        // OR
        case 0xB0: OP_OR_r(m_regs.b); break;
        case 0xB1: OP_OR_r(m_regs.c); break;
        case 0xB2: OP_OR_r(m_regs.d); break;
        case 0xB3: OP_OR_r(m_regs.e); break;
        case 0xB4: OP_OR_r(m_regs.h); break;
        case 0xB5: OP_OR_r(m_regs.l); break;
        case 0xB6: OP_OR_r(ReadByte(m_regs.hl)); break;
        case 0xB7: OP_OR_r(m_regs.a); break;
        case 0xF6: OP_OR_r(FetchByte()); break;

        // CP
        case 0xB8: OP_CP_r(m_regs.b); break;
        case 0xB9: OP_CP_r(m_regs.c); break;
        case 0xBA: OP_CP_r(m_regs.d); break;
        case 0xBB: OP_CP_r(m_regs.e); break;
        case 0xBC: OP_CP_r(m_regs.h); break;
        case 0xBD: OP_CP_r(m_regs.l); break;
        case 0xBE: OP_CP_r(ReadByte(m_regs.hl)); break;
        case 0xBF: OP_CP_r(m_regs.a); break;
        case 0xFE: OP_CP_r(FetchByte()); break;

        // INC/DEC 8-bit
        case 0x04: OP_INC_r(m_regs.b); break;
        case 0x0C: OP_INC_r(m_regs.c); break;
        case 0x14: OP_INC_r(m_regs.d); break;
        case 0x1C: OP_INC_r(m_regs.e); break;
        case 0x24: OP_INC_r(m_regs.h); break;
        case 0x2C: OP_INC_r(m_regs.l); break;
        case 0x34: OP_INC_HL(); break;
        case 0x3C: OP_INC_r(m_regs.a); break;

        case 0x05: OP_DEC_r(m_regs.b); break;
        case 0x0D: OP_DEC_r(m_regs.c); break;
        case 0x15: OP_DEC_r(m_regs.d); break;
        case 0x1D: OP_DEC_r(m_regs.e); break;
        case 0x25: OP_DEC_r(m_regs.h); break;
        case 0x2D: OP_DEC_r(m_regs.l); break;
        case 0x35: OP_DEC_HL(); break;
        case 0x3D: OP_DEC_r(m_regs.a); break;

        // 16-bit arithmetic
        case 0x09: OP_ADD_HL_rr(m_regs.bc); break;
        case 0x19: OP_ADD_HL_rr(m_regs.de); break;
        case 0x29: OP_ADD_HL_rr(m_regs.hl); break;
        case 0x39: OP_ADD_HL_rr(m_regs.sp); break;
        case 0xE8: OP_ADD_SP_e(); break;

        case 0x03: OP_INC_rr(m_regs.bc); break;
        case 0x13: OP_INC_rr(m_regs.de); break;
        case 0x23: OP_INC_rr(m_regs.hl); break;
        case 0x33: OP_INC_rr(m_regs.sp); break;

        case 0x0B: OP_DEC_rr(m_regs.bc); break;
        case 0x1B: OP_DEC_rr(m_regs.de); break;
        case 0x2B: OP_DEC_rr(m_regs.hl); break;
        case 0x3B: OP_DEC_rr(m_regs.sp); break;

        // Jumps
        case 0xC3: OP_JP_nn(); break;
        case 0xE9: OP_JP_HL(); break;
        case 0xC2: OP_JP_cc_nn(!GetFlag(FLAG_Z)); break;  // JP NZ,nn
        case 0xCA: OP_JP_cc_nn(GetFlag(FLAG_Z)); break;   // JP Z,nn
        case 0xD2: OP_JP_cc_nn(!GetFlag(FLAG_C)); break;  // JP NC,nn
        case 0xDA: OP_JP_cc_nn(GetFlag(FLAG_C)); break;   // JP C,nn

        case 0x18: OP_JR_e(); break;
        case 0x20: OP_JR_cc_e(!GetFlag(FLAG_Z)); break;  // JR NZ,e
        case 0x28: OP_JR_cc_e(GetFlag(FLAG_Z)); break;   // JR Z,e
        case 0x30: OP_JR_cc_e(!GetFlag(FLAG_C)); break;  // JR NC,e
        case 0x38: OP_JR_cc_e(GetFlag(FLAG_C)); break;   // JR C,e

        // Calls and returns
        case 0xCD: OP_CALL_nn(); break;
        case 0xC4: OP_CALL_cc_nn(!GetFlag(FLAG_Z)); break;  // CALL NZ,nn
        case 0xCC: OP_CALL_cc_nn(GetFlag(FLAG_Z)); break;   // CALL Z,nn
        case 0xD4: OP_CALL_cc_nn(!GetFlag(FLAG_C)); break;  // CALL NC,nn
        case 0xDC: OP_CALL_cc_nn(GetFlag(FLAG_C)); break;   // CALL C,nn

        case 0xC9: OP_RET(); break;
        case 0xC0: OP_RET_cc(!GetFlag(FLAG_Z)); break;  // RET NZ
        case 0xC8: OP_RET_cc(GetFlag(FLAG_Z)); break;   // RET Z
        case 0xD0: OP_RET_cc(!GetFlag(FLAG_C)); break;  // RET NC
        case 0xD8: OP_RET_cc(GetFlag(FLAG_C)); break;   // RET C
        case 0xD9: OP_RETI(); break;

        // RST
        case 0xC7: OP_RST(0x00); break;
        case 0xCF: OP_RST(0x08); break;
        case 0xD7: OP_RST(0x10); break;
        case 0xDF: OP_RST(0x18); break;
        case 0xE7: OP_RST(0x20); break;
        case 0xEF: OP_RST(0x28); break;
        case 0xF7: OP_RST(0x30); break;
        case 0xFF: OP_RST(0x38); break;

        // Rotates and shifts
        case 0x07: OP_RLCA(); break;
        case 0x17: OP_RLA(); break;
        case 0x0F: OP_RRCA(); break;
        case 0x1F: OP_RRA(); break;

        // Misc
        case 0x27: OP_DAA(); break;
        case 0x2F: OP_CPL(); break;
        case 0x3F: OP_CCF(); break;
        case 0x37: OP_SCF(); break;
        case 0x76: OP_HALT(); break;
        case 0x10: OP_STOP(); break;
        case 0xF3: OP_DI(); break;
        case 0xFB: OP_EI(); break;

        // CB prefix - extended instructions
        case 0xCB: {
            u8 cb_opcode = FetchByte();
            ExecuteCBInstruction(cb_opcode);
            break;
        }

        default:
            spdlog::error("Unknown opcode: 0x{:02X} at PC=0x{:04X}", opcode, m_regs.pc - 1);
            break;
    }
}
