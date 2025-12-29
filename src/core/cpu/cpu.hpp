#pragma once
#include "../types.hpp"
#include "../memory/memory.hpp"
#include "../scheduler/scheduler.hpp"

class CPU {
public:
    // CPU Flags (in F register)
    enum Flags : u8 {
        FLAG_C = 0x10,  // Carry flag (bit 4)
        FLAG_H = 0x20,  // Half-carry flag (bit 5)
        FLAG_N = 0x40,  // Subtract flag (bit 6)
        FLAG_Z = 0x80   // Zero flag (bit 7)
    };

    // Interrupt addresses
    enum InterruptVectors : u16 {
        INT_VBLANK  = 0x0040,
        INT_LCD     = 0x0048,
        INT_TIMER   = 0x0050,
        INT_SERIAL  = 0x0058,
        INT_JOYPAD  = 0x0060
    };

    CPU(Memory& memory, Scheduler& scheduler);
    ~CPU() = default;

    // Execute one instruction
    u32 Step();

    // Reset CPU to power-on state
    void Reset();

    // Interrupt handling
    void RequestInterrupt(u8 interrupt_bit);
    void ServiceInterrupts();

    // Get current state (for debugging)
    u16 GetPC() const { return m_regs.pc; }
    u16 GetSP() const { return m_regs.sp; }
    u8 GetA() const { return m_regs.a; }
    u8 GetF() const { return m_regs.f; }

    // IME (Interrupt Master Enable)
    bool IsIMEEnabled() const { return m_ime; }
    void SetIME(bool enabled) { m_ime = enabled; }

private:
    // Registers with union for efficient 8/16-bit access
    struct Registers {
        union {
            struct {
                u8 f;  // Flags (low byte)
                u8 a;  // Accumulator (high byte)
            };
            u16 af;
        };

        union {
            struct {
                u8 c;  // Low byte
                u8 b;  // High byte
            };
            u16 bc;
        };

        union {
            struct {
                u8 e;  // Low byte
                u8 d;  // High byte
            };
            u16 de;
        };

        union {
            struct {
                u8 l;  // Low byte
                u8 h;  // High byte
            };
            u16 hl;
        };

        u16 sp;  // Stack pointer
        u16 pc;  // Program counter
    } m_regs;

    Memory& m_memory;
    Scheduler& m_scheduler;

    bool m_ime;        // Interrupt Master Enable
    bool m_halted;     // CPU halted
    bool m_stopped;    // CPU stopped
    u32 m_cycles;      // Cycle counter for current instruction

    // Flag manipulation helpers
    FORCE_INLINE bool GetFlag(u8 flag) const {
        return (m_regs.f & flag) != 0;
    }

    FORCE_INLINE void SetFlag(u8 flag, bool value) {
        if (value) {
            m_regs.f |= flag;
        } else {
            m_regs.f &= ~flag;
        }
    }

    FORCE_INLINE void SetFlags(bool z, bool n, bool h, bool c) {
        m_regs.f = (z ? FLAG_Z : 0) | (n ? FLAG_N : 0) |
                   (h ? FLAG_H : 0) | (c ? FLAG_C : 0);
    }

    // Memory access helpers
    FORCE_INLINE u8 ReadByte(u16 address) {
        m_cycles += 4;
        return m_memory.Read(address);
    }

    FORCE_INLINE void WriteByte(u16 address, u8 value) {
        m_cycles += 4;
        m_memory.Write(address, value);
    }

    FORCE_INLINE u16 ReadWord(u16 address) {
        m_cycles += 8;
        return m_memory.Read16(address);
    }

    FORCE_INLINE void WriteWord(u16 address, u16 value) {
        m_cycles += 8;
        m_memory.Write16(address, value);
    }

    // Fetch helpers
    FORCE_INLINE u8 FetchByte() {
        return ReadByte(m_regs.pc++);
    }

    FORCE_INLINE u16 FetchWord() {
        u16 value = ReadWord(m_regs.pc);
        m_regs.pc += 2;
        return value;
    }

    // Stack operations
    FORCE_INLINE void Push(u16 value) {
        m_regs.sp -= 2;
        WriteWord(m_regs.sp, value);
    }

    FORCE_INLINE u16 Pop() {
        u16 value = ReadWord(m_regs.sp);
        m_regs.sp += 2;
        return value;
    }

    // Instruction implementations (will be in instructions.cpp)
    void ExecuteInstruction(u8 opcode);
    void ExecuteCBInstruction(u8 opcode);

    // Opcode handlers - 8-bit loads
    void OP_LD_r_r(u8& dest, u8 src);
    void OP_LD_r_n(u8& reg);
    void OP_LD_r_HL(u8& reg);
    void OP_LD_HL_r(u8 reg);
    void OP_LD_HL_n();
    void OP_LD_A_BC();
    void OP_LD_A_DE();
    void OP_LD_A_nn();
    void OP_LD_BC_A();
    void OP_LD_DE_A();
    void OP_LD_nn_A();
    void OP_LDH_A_n();
    void OP_LDH_n_A();
    void OP_LD_A_C();
    void OP_LD_C_A();
    void OP_LDI_HL_A();
    void OP_LDI_A_HL();
    void OP_LDD_HL_A();
    void OP_LDD_A_HL();

    // 16-bit loads
    void OP_LD_rr_nn(u16& reg);
    void OP_LD_SP_HL();
    void OP_PUSH(u16 reg);
    void OP_POP(u16& reg);
    void OP_LD_nn_SP();
    void OP_LD_HL_SP_e();

    // 8-bit arithmetic
    void OP_ADD_A_r(u8 value);
    void OP_ADC_A_r(u8 value);
    void OP_SUB_r(u8 value);
    void OP_SBC_A_r(u8 value);
    void OP_AND_r(u8 value);
    void OP_XOR_r(u8 value);
    void OP_OR_r(u8 value);
    void OP_CP_r(u8 value);
    void OP_INC_r(u8& reg);
    void OP_DEC_r(u8& reg);
    void OP_INC_HL();
    void OP_DEC_HL();

    // 16-bit arithmetic
    void OP_ADD_HL_rr(u16 value);
    void OP_INC_rr(u16& reg);
    void OP_DEC_rr(u16& reg);
    void OP_ADD_SP_e();

    // Jumps
    void OP_JP_nn();
    void OP_JP_HL();
    void OP_JP_cc_nn(bool condition);
    void OP_JR_e();
    void OP_JR_cc_e(bool condition);

    // Calls and returns
    void OP_CALL_nn();
    void OP_CALL_cc_nn(bool condition);
    void OP_RET();
    void OP_RET_cc(bool condition);
    void OP_RETI();
    void OP_RST(u8 vector);

    // Rotates and shifts
    void OP_RLCA();
    void OP_RLA();
    void OP_RRCA();
    void OP_RRA();
    void OP_RLC(u8& reg);
    void OP_RL(u8& reg);
    void OP_RRC(u8& reg);
    void OP_RR(u8& reg);
    void OP_SLA(u8& reg);
    void OP_SRA(u8& reg);
    void OP_SRL(u8& reg);
    void OP_SWAP(u8& reg);

    // Bit operations
    void OP_BIT(u8 bit, u8 value);
    void OP_SET(u8 bit, u8& reg);
    void OP_RES(u8 bit, u8& reg);

    // Misc
    void OP_DAA();
    void OP_CPL();
    void OP_CCF();
    void OP_SCF();
    void OP_NOP();
    void OP_HALT();
    void OP_STOP();
    void OP_DI();
    void OP_EI();
};
