#include "rv/decode.hpp"

namespace rv {
    const char* op_name(Op op) {
        // Keep names in Op enum order; traces and instruction-mix counters use this table.
        static constexpr const char* names[] = {"lui", "auipc", "jal", "jalr", "beq", "bne", "blt",
            "bge", "bltu", "bgeu", "lb", "lh", "lw", "lbu", "lhu", "sb", "sh", "sw", "addi", "slti",
            "sltiu", "xori", "ori", "andi", "slli", "srli", "srai", "add", "sub", "sll", "slt",
            "sltu", "xor", "srl", "sra", "or", "and", "fence", "ecall", "ebreak", "illegal"};

        return names[static_cast<unsigned>(op)];
    }

    Decoded decode(Word w) {
        // Every supported machine instruction is 32 bits. Number bits from 0 (lowest).
        // R-type layout: [funct7:7][rs2:5][rs1:5][funct3:3][rd:5][opcode:7].
        // Shift right to locate a field, then mask off its width: 31 = five 1 bits.
        Decoded d;
        d.rd = (w >> 7) & 31;
        d.rs1 = (w >> 15) & 31;
        d.rs2 = (w >> 20) & 31;

        // opcode selects a family; funct3/funct7 distinguish members such as ADD/SUB.
        // Other formats reuse some register-field positions for immediate bits instead.
        const auto opcode = w & 127, f3 = (w >> 12) & 7, f7 = w >> 25;
        // I-type instructions hold a signed 12-bit constant in bits 31..20.
        const auto iimm = sext(w >> 20, 12);

        switch (opcode) {
            // U-type: the encoded upper constant is already in its final bit positions.
            case 0x37:
                d.op = Op::LUI;
                d.imm = w & 0xfffff000;
                d.writes_rd = true;
                break;

            case 0x17:
                d.op = Op::AUIPC;
                d.imm = w & 0xfffff000;
                d.writes_rd = true;
                break;

            case 0x6f:
                d.op = Op::JAL;
                d.writes_rd = true;
                // J-type scatters imm[20|10:1|11|19:12] across the word. Reassemble
                // a signed byte offset; its omitted bit 0 is always zero.
                d.imm = sext(((w >> 31) << 20) | (((w >> 12) & 255) << 12) |
                                 (((w >> 20) & 1) << 11) | (((w >> 21) & 1023) << 1),
                    21);
                break;

            case 0x67:
                // JALR is an I-type address calculation, with only funct3=0 supported.
                if (f3 == 0) {
                    d.op = Op::JALR;
                    d.uses_rs1 = d.writes_rd = true;
                    d.imm = iimm;
                }
                break;

            case 0x63: {
                // B-type compares two registers and never writes rd. Reserved funct3
                // values map to Illegal instead of being interpreted as another branch.
                static constexpr Op branches[] = {Op::BEQ, Op::BNE, Op::Illegal, Op::Illegal,
                    Op::BLT, Op::BGE, Op::BLTU, Op::BGEU};
                d.op = branches[f3];
                d.uses_rs1 = d.uses_rs2 = true;
                // B-type imm[12|10:5|4:1|11] is a signed PC-relative byte offset.
                d.imm = sext(((w >> 31) << 12) | (((w >> 7) & 1) << 11) | (((w >> 25) & 63) << 5) |
                                 (((w >> 8) & 15) << 1),
                    13);
                break;
            }

            case 0x03: {
                // Loads use rs1 as an address base and write rd. funct3 selects
                // byte/halfword/word width and signed versus zero extension.
                static constexpr Op loads[] = {Op::LB, Op::LH, Op::LW, Op::Illegal, Op::LBU,
                    Op::LHU, Op::Illegal, Op::Illegal};
                d.op = loads[f3];
                d.uses_rs1 = d.writes_rd = d.load = true;
                d.imm = iimm;
                d.width = f3 == 0 || f3 == 4 ? 1 : f3 == 1 || f3 == 5 ? 2 : 4;
                break;
            }

            case 0x23:
                // S-type: rs1 is the base, rs2 supplies data. Its 12-bit offset is
                // split between bits 31..25 and 11..7 (there is no destination field).
                d.op = f3 == 0 ? Op::SB : f3 == 1 ? Op::SH : f3 == 2 ? Op::SW : Op::Illegal;
                d.uses_rs1 = d.uses_rs2 = d.store = true;
                d.imm = sext(((w >> 25) << 5) | ((w >> 7) & 31), 12);
                d.width = f3 == 0 ? 1 : f3 == 1 ? 2 : 4;
                break;

            case 0x13: {
                // I-type ALU: one register source plus an immediate. Bits in the rs2
                // position belong to that immediate and must not create a dependency.
                static constexpr Op ops[] = {
                    Op::ADDI, Op::SLLI, Op::SLTI, Op::SLTIU, Op::XORI, Op::SRLI, Op::ORI, Op::ANDI};
                d.op = ops[f3];
                d.uses_rs1 = d.writes_rd = true;
                d.imm = iimm;
                if (f3 == 1 || f3 == 5) {
                    // Shift immediates use only a 5-bit count. Higher bits select
                    // arithmetic versus logical shift, or make the encoding illegal.
                    d.imm = (w >> 20) & 31;
                    if (f3 == 5 && f7 == 0x20) {
                        d.op = Op::SRAI;
                    } else if (f7 != 0) {
                        d.op = Op::Illegal;
                    }
                }
                break;
            }

            case 0x33: {
                // R-type ALU: two register sources. funct7=0x20 selects SUB/SRA;
                // rejecting other funct7 values also rejects M-extension MUL/DIV/REM.
                static constexpr Op ops[] = {
                    Op::ADD, Op::SLL, Op::SLT, Op::SLTU, Op::XOR, Op::SRL, Op::OR, Op::AND};
                d.op = ops[f3];
                d.uses_rs1 = d.uses_rs2 = d.writes_rd = true;
                if (f7 == 0x20 && f3 == 0) {
                    d.op = Op::SUB;
                } else if (f7 == 0x20 && f3 == 5) {
                    d.op = Op::SRA;
                } else if (f7 != 0) {
                    d.op = Op::Illegal;
                }
                break;
            }

            case 0x0f:
                // FENCE ordering is a no-op in this single-hart, uncached environment.
                // fm != 0 (including TSO) is outside our selected base environment.
                if (f3 == 0 && (w >> 28) == 0) {
                    d.op = Op::FENCE;
                }
                break;

            case 0x73:
                // Match exact SYSTEM words; this environment has no CSR/privileged support.
                // Decoding these does not stop the CPU: they still have to reach commit.
                if (w == 0x00000073) {
                    d.op = Op::ECALL;
                }
                if (w == 0x00100073) {
                    d.op = Op::EBREAK;
                }
                break;
            default:
                break;
        }

        if (d.op == Op::Illegal) {
            // An invalid word must not look like a register writer, load, or store
            // while it travels through hazard detection toward the trap boundary.
            d.uses_rs1 = d.uses_rs2 = d.writes_rd = d.load = d.store = false;
        }

        return d;
    }
} // namespace rv
