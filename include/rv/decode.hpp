#pragma once

#include "rv/types.hpp"

namespace rv {
    // Internal operation names, not the numeric opcode bits stored in an ELF/bin image.
    // In the comments, rd is the destination and rs1/rs2 are source register numbers.
    // Full syntax, worked examples, and register roles: docs/INSTRUCTION_REFERENCE.md.
    enum class Op {
        LUI,   // Put a 20-bit constant in rd's upper bits; lower 12 bits become zero.
        AUIPC, // Add that upper-immediate constant to this instruction's PC.
        JAL,   // Jump relative to PC; write the return address (PC + 4) into rd.
        JALR,  // Jump to (rs1 + offset) with bit 0 cleared; also write a return address.

        // Keep these branches contiguous: execute() uses this range to identify branches.
        BEQ,  // Branch when the two source values are equal; no register is written.
        BNE,  // Branch when the two source values differ.
        BLT,  // Branch when signed rs1 < signed rs2.
        BGE,  // Branch when signed rs1 >= signed rs2.
        BLTU, // The same less-than comparison using unsigned values.
        BGEU, // The same greater-or-equal comparison using unsigned values.

        LB,  // Load 1 byte, sign-extend it, and write rd.
        LH,  // Load 2 bytes, sign-extend them, and write rd.
        LW,  // Load a 4-byte word into rd.
        LBU, // Load 1 byte and fill rd's upper bits with zeros.
        LHU, // Load 2 bytes and fill rd's upper bits with zeros.
        SB,  // Store the low 8 bits of rs2 at address rs1 + offset.
        SH,  // Store the low 16 bits of rs2; no register is written.
        SW,  // Store all 32 bits of rs2; no register is written.

        ADDI,  // rd = rs1 + sign-extended immediate.
        SLTI,  // rd = 1 if signed rs1 < signed immediate, otherwise 0.
        SLTIU, // Compare as unsigned, after sign-extending the immediate.
        XORI,  // Bitwise XOR with the sign-extended immediate.
        ORI,   // Bitwise OR with the sign-extended immediate.
        ANDI,  // Bitwise AND with the sign-extended immediate.
        SLLI,  // Shift left by a constant count, 0..31.
        SRLI,  // Shift right by a constant count, shifting zeros in.
        SRAI,  // Shift right by a constant count, copying the sign bit in.

        ADD,  // rd = rs1 + rs2, wrapping to 32 bits.
        SUB,  // rd = rs1 - rs2, wrapping to 32 bits.
        SLL,  // Shift left by the low 5 bits of rs2.
        SLT,  // rd = 1 if signed rs1 < signed rs2, otherwise 0.
        SLTU, // The same comparison using unsigned values.
        XOR,  // Bitwise exclusive OR of the source values.
        SRL,  // Logical right shift; new upper bits are zero.
        SRA,  // Arithmetic right shift; new upper bits copy the sign bit.
        OR,   // Bitwise OR of the source values.
        AND,  // Bitwise AND of the source values.

        FENCE,  // Memory ordering; no work in this single-hart, uncached model.
        ECALL,  // Request the environment; here it becomes a terminal fault, not a syscall.
        EBREAK, // Breakpoint instruction; this project's convention treats it as completion.
        Illegal // Unsupported or malformed encoding; becomes a terminal fault at commit.
    };

    // The decoder describes an instruction without executing it or reading any registers.
    struct Decoded {
        Op op = Op::Illegal;
        // These are register indices, not the values currently held in those registers.
        unsigned rd = 0, rs1 = 0, rs2 = 0;
        // An immediate is a constant encoded inside the instruction word.
        Word imm = 0;

        // Only these flags tell the pipeline which fields are real dependencies.
        // For example, store bits in the rd position actually belong to its immediate.
        bool uses_rs1 = false, uses_rs2 = false, writes_rd = false;

        bool load = false, store = false;
        unsigned width = 0; // Memory-access size in bytes: 1, 2, or 4.
    };

    Decoded decode(Word word);
    const char* op_name(Op op);
} // namespace rv
