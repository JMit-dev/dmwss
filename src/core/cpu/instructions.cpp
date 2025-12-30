#include "cpu.hpp"

// ============================================================================
// 8-bit Load Instructions
// ============================================================================

void CPU::OP_LD_r_r(u8& dest, u8 src) {
    dest = src;
    // LD r,r takes 4 T-cycles total (just the opcode fetch)
}

void CPU::OP_LD_r_n(u8& reg) {
    reg = FetchByte();  // FetchByte adds 4 T-cycles for immediate byte
    // Total: 4 (opcode) + 4 (immediate) = 8 T-cycles
}

void CPU::OP_LD_r_HL(u8& reg) {
    reg = ReadByte(m_regs.hl);  // ReadByte already adds 4 T-cycles
}

void CPU::OP_LD_HL_r(u8 reg) {
    WriteByte(m_regs.hl, reg);  // WriteByte already adds 4 T-cycles
}

void CPU::OP_LD_HL_n() {
    u8 value = FetchByte();  // FetchByte adds 4 T-cycles
    WriteByte(m_regs.hl, value);  // WriteByte adds 4 T-cycles
    // Total: 4 (opcode) + 4 (immediate) + 4 (write) = 12 T-cycles
}

void CPU::OP_LD_A_BC() {
    m_regs.a = ReadByte(m_regs.bc);  // ReadByte already adds 4 T-cycles
}

void CPU::OP_LD_A_DE() {
    m_regs.a = ReadByte(m_regs.de);  // ReadByte already adds 4 T-cycles
}

void CPU::OP_LD_A_nn() {
    u16 address = FetchWord();  // ReadWord adds 8 T-cycles
    m_regs.a = ReadByte(address);  // ReadByte adds 4 T-cycles
}

void CPU::OP_LD_BC_A() {
    WriteByte(m_regs.bc, m_regs.a);  // WriteByte already adds 4 T-cycles
}

void CPU::OP_LD_DE_A() {
    WriteByte(m_regs.de, m_regs.a);  // WriteByte already adds 4 T-cycles
}

void CPU::OP_LD_nn_A() {
    u16 address = FetchWord();  // ReadWord adds 8 T-cycles
    WriteByte(address, m_regs.a);  // WriteByte adds 4 T-cycles
}

void CPU::OP_LDH_A_n() {
    u8 offset = FetchByte();  // ReadByte adds 4 T-cycles
    m_regs.a = ReadByte(0xFF00 + offset);  // ReadByte adds 4 T-cycles
}

void CPU::OP_LDH_n_A() {
    u8 offset = FetchByte();  // ReadByte adds 4 T-cycles
    WriteByte(0xFF00 + offset, m_regs.a);  // WriteByte adds 4 T-cycles
}

void CPU::OP_LD_A_C() {
    m_regs.a = ReadByte(0xFF00 + m_regs.c);  // ReadByte already adds 4 T-cycles
}

void CPU::OP_LD_C_A() {
    WriteByte(0xFF00 + m_regs.c, m_regs.a);  // WriteByte already adds 4 T-cycles
}

void CPU::OP_LDI_HL_A() {
    WriteByte(m_regs.hl, m_regs.a);  // WriteByte already adds 4 T-cycles
    m_regs.hl++;
}

void CPU::OP_LDI_A_HL() {
    m_regs.a = ReadByte(m_regs.hl);  // ReadByte already adds 4 T-cycles
    m_regs.hl++;
}

void CPU::OP_LDD_HL_A() {
    WriteByte(m_regs.hl, m_regs.a);  // WriteByte already adds 4 T-cycles
    m_regs.hl--;
}

void CPU::OP_LDD_A_HL() {
    m_regs.a = ReadByte(m_regs.hl);  // ReadByte already adds 4 T-cycles
    m_regs.hl--;
}

// ============================================================================
// 16-bit Load Instructions
// ============================================================================

void CPU::OP_LD_rr_nn(u16& reg) {
    reg = FetchWord();  // FetchWord adds 8 T-cycles
    // Total: 4 (opcode) + 8 (immediate word) = 12 T-cycles
}

void CPU::OP_LD_SP_HL() {
    m_regs.sp = m_regs.hl;
    m_cycles += 8;
}

void CPU::OP_PUSH(u16 reg) {
    Push(reg);  // WriteWord adds 8 T-cycles
    m_cycles += 8;  // PUSH takes 16 T-cycles total
}

void CPU::OP_POP(u16& reg) {
    reg = Pop();  // ReadWord adds 8 T-cycles
    // Special case: F register only uses upper 4 bits
    if (&reg == &m_regs.af) {
        m_regs.f &= 0xF0;
    }
    m_cycles += 4;  // POP takes 12 T-cycles total
}

void CPU::OP_LD_nn_SP() {
    u16 address = FetchWord();  // FetchWord adds 8 T-cycles
    WriteWord(address, m_regs.sp);  // WriteWord adds 8 T-cycles
    // Total: 4 (opcode) + 8 (address) + 8 (write) = 20 T-cycles
}

void CPU::OP_LD_HL_SP_e() {
    s8 offset = static_cast<s8>(FetchByte());
    u32 result = m_regs.sp + offset;
    
    // Set flags
    SetFlag(FLAG_Z, false);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, ((m_regs.sp & 0x0F) + (offset & 0x0F)) > 0x0F);
    SetFlag(FLAG_C, ((m_regs.sp & 0xFF) + (offset & 0xFF)) > 0xFF);
    
    m_regs.hl = static_cast<u16>(result);
    m_cycles += 4;
}

// ============================================================================
// 8-bit Arithmetic Instructions
// ============================================================================

void CPU::OP_ADD_A_r(u8 value) {
    u16 result = m_regs.a + value;

    SetFlag(FLAG_Z, (result & 0xFF) == 0);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, ((m_regs.a & 0x0F) + (value & 0x0F)) > 0x0F);
    SetFlag(FLAG_C, result > 0xFF);

    m_regs.a = static_cast<u8>(result);
    // ADD takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_ADC_A_r(u8 value) {
    u8 carry = GetFlag(FLAG_C) ? 1 : 0;
    u16 result = m_regs.a + value + carry;

    SetFlag(FLAG_Z, (result & 0xFF) == 0);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, ((m_regs.a & 0x0F) + (value & 0x0F) + carry) > 0x0F);
    SetFlag(FLAG_C, result > 0xFF);

    m_regs.a = static_cast<u8>(result);
    // ADC takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_SUB_r(u8 value) {
    u16 result = m_regs.a - value;

    SetFlag(FLAG_Z, (result & 0xFF) == 0);
    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, (m_regs.a & 0x0F) < (value & 0x0F));
    SetFlag(FLAG_C, m_regs.a < value);

    m_regs.a = static_cast<u8>(result);
    // SUB takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_SBC_A_r(u8 value) {
    u8 carry = GetFlag(FLAG_C) ? 1 : 0;
    u16 result = m_regs.a - value - carry;

    SetFlag(FLAG_Z, (result & 0xFF) == 0);
    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, (m_regs.a & 0x0F) < (value & 0x0F) + carry);
    SetFlag(FLAG_C, m_regs.a < value + carry);

    m_regs.a = static_cast<u8>(result);
    // SBC takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_AND_r(u8 value) {
    m_regs.a &= value;

    SetFlags(m_regs.a == 0, false, true, false);
    // AND takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_XOR_r(u8 value) {
    m_regs.a ^= value;

    SetFlags(m_regs.a == 0, false, false, false);
    // XOR takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_OR_r(u8 value) {
    m_regs.a |= value;

    SetFlags(m_regs.a == 0, false, false, false);
    // OR takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_CP_r(u8 value) {
    u16 result = m_regs.a - value;

    SetFlag(FLAG_Z, (result & 0xFF) == 0);
    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, (m_regs.a & 0x0F) < (value & 0x0F));
    SetFlag(FLAG_C, m_regs.a < value);

    // CP takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_INC_r(u8& reg) {
    reg++;

    SetFlag(FLAG_Z, reg == 0);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, (reg & 0x0F) == 0);

    // INC r takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_DEC_r(u8& reg) {
    reg--;

    SetFlag(FLAG_Z, reg == 0);
    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, (reg & 0x0F) == 0x0F);

    // DEC r takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_INC_HL() {
    u8 value = ReadByte(m_regs.hl);  // ReadByte adds 4 T-cycles
    value++;
    WriteByte(m_regs.hl, value);  // WriteByte adds 4 T-cycles

    SetFlag(FLAG_Z, value == 0);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, (value & 0x0F) == 0);

    m_cycles += 4;  // INC (HL) takes 12 T-cycles total
}

void CPU::OP_DEC_HL() {
    u8 value = ReadByte(m_regs.hl);  // ReadByte adds 4 T-cycles
    value--;
    WriteByte(m_regs.hl, value);  // WriteByte adds 4 T-cycles

    SetFlag(FLAG_Z, value == 0);
    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, (value & 0x0F) == 0x0F);

    m_cycles += 4;  // DEC (HL) takes 12 T-cycles total
}

// ============================================================================
// 16-bit Arithmetic Instructions
// ============================================================================

void CPU::OP_ADD_HL_rr(u16 value) {
    u32 result = m_regs.hl + value;
    
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, ((m_regs.hl & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF);
    SetFlag(FLAG_C, result > 0xFFFF);
    
    m_regs.hl = static_cast<u16>(result);
    m_cycles += 8;
}

void CPU::OP_INC_rr(u16& reg) {
    reg++;
    m_cycles += 8;
}

void CPU::OP_DEC_rr(u16& reg) {
    reg--;
    m_cycles += 8;
}

void CPU::OP_ADD_SP_e() {
    s8 offset = static_cast<s8>(FetchByte());
    u32 result = m_regs.sp + offset;
    
    SetFlag(FLAG_Z, false);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, ((m_regs.sp & 0x0F) + (offset & 0x0F)) > 0x0F);
    SetFlag(FLAG_C, ((m_regs.sp & 0xFF) + (offset & 0xFF)) > 0xFF);
    
    m_regs.sp = static_cast<u16>(result);
    m_cycles += 12;
}

// ============================================================================
// Jump Instructions
// ============================================================================

void CPU::OP_JP_nn() {
    m_regs.pc = FetchWord();  // ReadWord adds 8 T-cycles
    m_cycles += 4;  // JP nn takes 16 T-cycles total
}

void CPU::OP_JP_HL() {
    m_regs.pc = m_regs.hl;
    m_cycles += 4;
}

void CPU::OP_JP_cc_nn(bool condition) {
    u16 address = FetchWord();  // ReadWord adds 8 T-cycles
    if (condition) {
        m_regs.pc = address;
        m_cycles += 4;  // Taken: 16 T-cycles total
    }
    // Not taken: 12 T-cycles total (ReadWord already added 8)
}

void CPU::OP_JR_e() {
    s8 offset = static_cast<s8>(FetchByte());  // ReadByte adds 4 T-cycles
    m_regs.pc += offset;
    m_cycles += 4;  // JR takes 12 T-cycles total
}

void CPU::OP_JR_cc_e(bool condition) {
    s8 offset = static_cast<s8>(FetchByte());  // ReadByte adds 4 T-cycles
    if (condition) {
        m_regs.pc += offset;
        m_cycles += 4;  // Taken: 12 T-cycles total
    }
    // Not taken: 8 T-cycles total (ReadByte already added 4)
}

// ============================================================================
// Call and Return Instructions
// ============================================================================

void CPU::OP_CALL_nn() {
    u16 address = FetchWord();  // ReadWord adds 8 T-cycles
    Push(m_regs.pc);  // WriteWord adds 8 T-cycles
    m_regs.pc = address;
    m_cycles += 4;  // CALL takes 24 T-cycles total
}

void CPU::OP_CALL_cc_nn(bool condition) {
    u16 address = FetchWord();  // ReadWord adds 8 T-cycles
    if (condition) {
        Push(m_regs.pc);  // WriteWord adds 8 T-cycles
        m_regs.pc = address;
        m_cycles += 4;  // Taken: 24 T-cycles total
    }
    // Not taken: 12 T-cycles total (ReadWord already added 8)
}

void CPU::OP_RET() {
    m_regs.pc = Pop();  // ReadWord adds 8 T-cycles
    m_cycles += 8;  // RET takes 16 T-cycles total
}

void CPU::OP_RET_cc(bool condition) {
    if (condition) {
        m_regs.pc = Pop();  // ReadWord adds 8 T-cycles
        m_cycles += 8;  // Taken: 20 T-cycles total
    } else {
        m_cycles += 4;   // Not taken: 8 T-cycles total
    }
}

void CPU::OP_RETI() {
    m_regs.pc = Pop();  // ReadWord adds 8 T-cycles
    m_ime = true;
    m_cycles += 8;  // RETI takes 16 T-cycles total
}

void CPU::OP_RST(u8 vector) {
    Push(m_regs.pc);
    m_regs.pc = vector;
    m_cycles += 16;  // RST takes 4 M-cycles = 16 T-cycles
}

// ============================================================================
// Rotate and Shift Instructions
// ============================================================================

void CPU::OP_RLCA() {
    bool carry = (m_regs.a & 0x80) != 0;
    m_regs.a = (m_regs.a << 1) | (carry ? 1 : 0);

    SetFlags(false, false, false, carry);
    // RLCA takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_RLA() {
    bool carry = (m_regs.a & 0x80) != 0;
    m_regs.a = (m_regs.a << 1) | (GetFlag(FLAG_C) ? 1 : 0);

    SetFlags(false, false, false, carry);
    // RLA takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_RRCA() {
    bool carry = (m_regs.a & 0x01) != 0;
    m_regs.a = (m_regs.a >> 1) | (carry ? 0x80 : 0);

    SetFlags(false, false, false, carry);
    // RRCA takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_RRA() {
    bool carry = (m_regs.a & 0x01) != 0;
    m_regs.a = (m_regs.a >> 1) | (GetFlag(FLAG_C) ? 0x80 : 0);

    SetFlags(false, false, false, carry);
    // RRA takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_RLC(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg = (reg << 1) | (carry ? 1 : 0);
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_RL(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg = (reg << 1) | (GetFlag(FLAG_C) ? 1 : 0);
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_RRC(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg = (reg >> 1) | (carry ? 0x80 : 0);
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_RR(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg = (reg >> 1) | (GetFlag(FLAG_C) ? 0x80 : 0);
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_SLA(u8& reg) {
    bool carry = (reg & 0x80) != 0;
    reg <<= 1;
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_SRA(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    u8 msb = reg & 0x80;
    reg = (reg >> 1) | msb;
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_SRL(u8& reg) {
    bool carry = (reg & 0x01) != 0;
    reg >>= 1;
    
    SetFlags(reg == 0, false, false, carry);
    m_cycles += 8;
}

void CPU::OP_SWAP(u8& reg) {
    reg = ((reg & 0x0F) << 4) | ((reg & 0xF0) >> 4);
    
    SetFlags(reg == 0, false, false, false);
    m_cycles += 8;
}

// ============================================================================
// Bit Operations
// ============================================================================

void CPU::OP_BIT(u8 bit, u8 value) {
    bool is_set = (value & (1 << bit)) != 0;
    
    SetFlag(FLAG_Z, !is_set);
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, true);
    
    m_cycles += 8;
}

void CPU::OP_SET(u8 bit, u8& reg) {
    reg |= (1 << bit);
    m_cycles += 8;
}

void CPU::OP_RES(u8 bit, u8& reg) {
    reg &= ~(1 << bit);
    m_cycles += 8;
}

// ============================================================================
// Miscellaneous Instructions
// ============================================================================

void CPU::OP_DAA() {
    u8 a = m_regs.a;

    if (!GetFlag(FLAG_N)) {
        if (GetFlag(FLAG_C) || a > 0x99) {
            a += 0x60;
            SetFlag(FLAG_C, true);
        }
        if (GetFlag(FLAG_H) || (a & 0x0F) > 0x09) {
            a += 0x06;
        }
    } else {
        if (GetFlag(FLAG_C)) {
            a -= 0x60;
        }
        if (GetFlag(FLAG_H)) {
            a -= 0x06;
        }
    }

    m_regs.a = a;
    SetFlag(FLAG_Z, m_regs.a == 0);
    SetFlag(FLAG_H, false);

    // DAA takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_CPL() {
    m_regs.a = ~m_regs.a;

    SetFlag(FLAG_N, true);
    SetFlag(FLAG_H, true);

    // CPL takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_CCF() {
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, false);
    SetFlag(FLAG_C, !GetFlag(FLAG_C));

    // CCF takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_SCF() {
    SetFlag(FLAG_N, false);
    SetFlag(FLAG_H, false);
    SetFlag(FLAG_C, true);

    // SCF takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_NOP() {
    // NOP takes 4 T-cycles total (just the opcode fetch, already counted)
}

void CPU::OP_HALT() {
    m_halted = true;
    // HALT takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_STOP() {
    m_stopped = true;
    // STOP takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_DI() {
    m_ime = false;
    // DI takes 4 T-cycles (just opcode fetch)
}

void CPU::OP_EI() {
    m_ime = true;
    // EI takes 4 T-cycles (just opcode fetch)
}

// ============================================================================
// CB-prefixed Instructions
// ============================================================================

void CPU::ExecuteCBInstruction(u8 opcode) {
    u8 reg_index = opcode & 0x07;
    u8 bit = (opcode >> 3) & 0x07;
    u8 op_type = (opcode >> 6) & 0x03;
    
    // Helper to get register by index
    auto get_reg = [&]() -> u8& {
        switch (reg_index) {
            case 0: return m_regs.b;
            case 1: return m_regs.c;
            case 2: return m_regs.d;
            case 3: return m_regs.e;
            case 4: return m_regs.h;
            case 5: return m_regs.l;
            case 7: return m_regs.a;
            default: {
                static u8 dummy = 0;
                return dummy;
            }
        }
    };
    
    // (HL) operations need special handling
    if (reg_index == 6) {
        u8 value = ReadByte(m_regs.hl);

        switch (op_type) {
            case 0: {  // Rotates/shifts - 16 T-cycles (4 CB + 4 read + 4 op + 4 write)
                u8 sub_op = (opcode >> 3) & 0x07;
                m_cycles -= 8;  // Undo the 8 cycles added by OP_ function
                switch (sub_op) {
                    case 0: OP_RLC(value); break;
                    case 1: OP_RRC(value); break;
                    case 2: OP_RL(value); break;
                    case 3: OP_RR(value); break;
                    case 4: OP_SLA(value); break;
                    case 5: OP_SRA(value); break;
                    case 6: OP_SWAP(value); break;
                    case 7: OP_SRL(value); break;
                }
                WriteByte(m_regs.hl, value);
                m_cycles += 12;  // Add correct total: read(4) + write(4) + internal(4)
                break;
            }
            case 1: OP_BIT(bit, value); break;  // BIT (HL) - 12 T-cycles (4 CB + 4 read + 4 test)
            case 2: value &= ~(1 << bit); WriteByte(m_regs.hl, value); m_cycles += 8; break;  // RES (HL) - 16 T-cycles
            case 3: value |= (1 << bit); WriteByte(m_regs.hl, value); m_cycles += 8; break;  // SET (HL) - 16 T-cycles
        }
    } else {
        u8& reg = get_reg();
        
        switch (op_type) {
            case 0: {  // Rotates/shifts
                u8 sub_op = (opcode >> 3) & 0x07;
                switch (sub_op) {
                    case 0: OP_RLC(reg); break;
                    case 1: OP_RRC(reg); break;
                    case 2: OP_RL(reg); break;
                    case 3: OP_RR(reg); break;
                    case 4: OP_SLA(reg); break;
                    case 5: OP_SRA(reg); break;
                    case 6: OP_SWAP(reg); break;
                    case 7: OP_SRL(reg); break;
                }
                break;
            }
            case 1: OP_BIT(bit, reg); break;
            case 2: OP_RES(bit, reg); break;
            case 3: OP_SET(bit, reg); break;
        }
    }
}
