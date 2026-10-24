#include "rv/execute.hpp"

namespace rv {
    // Arithmetic right shift copies the old sign bit into the vacated upper bits.
    // Build that behavior explicitly using unsigned values so it is host-independent.
    static Word asr(Word v, Word n) {
        n &= 31;
        if (!n) {
            // Avoid the undefined C++ expression that would shift a 32-bit mask by 32.
            return v;
        }

        return (v >> n) | ((v & 0x80000000u) ? (~Word(0) << (32 - n)) : 0);
    }

    Effect execute(const Decoded& d, Address pc, Word word, Word a, Word b) {
        Effect e;
        // Instructions are four bytes, so ordinary execution falls through to PC + 4.
        // d.rs1/rs2 are indices; a/b are the source values supplied by the scheduler.
        e.next_pc = pc + 4;

        switch (d.op) {
            case Op::LUI:
                // The decoder has already put the U-immediate in bits 31..12.
                e.value = d.imm;
                break;
            case Op::AUIPC:
                // PC-relative address construction, often followed by ADDI in a 'la' expansion.
                e.value = pc + d.imm;
                break;

            case Op::JAL:
                // Save the address AFTER this instruction, then jump relative to this PC.
                // rd=x0 discards the link at commit, making this an unconditional 'j'.
                e.value = pc + 4;
                e.next_pc = pc + d.imm;
                e.redirect = true;
                break;
            case Op::JALR:
                // The target is computed from the OLD rs1 value even when rd == rs1.
                // Bit 0 is cleared by the ISA; bit 1 can still cause an alignment fault.
                e.value = pc + 4;
                e.next_pc = (a + d.imm) & ~Word(1);
                e.redirect = true;
                break;

            case Op::BEQ:
                // Branches only choose control flow; they do not write a true/false register.
                // Signed and unsigned branches interpret the same source bits differently.
                e.redirect = a == b;
                break;
            case Op::BNE:
                e.redirect = a != b;
                break;
            case Op::BLT:
                e.redirect = signed_value(a) < signed_value(b);
                break;
            case Op::BGE:
                e.redirect = signed_value(a) >= signed_value(b);
                break;
            case Op::BLTU:
                e.redirect = a < b;
                break;
            case Op::BGEU:
                e.redirect = a >= b;
                break;

            case Op::LB:
            case Op::LH:
            case Op::LW:
            case Op::LBU:
            case Op::LHU:
            case Op::SB:
            case Op::SH:
            case Op::SW:
                // EX computes a byte address; it does NOT read/write RAM here.
                // MEM later reads loads or validates stores; only commit() writes bytes.
                e.address = a + d.imm;
                e.store_value = b;
                break;

            case Op::ADDI:
                // Word is uint32_t, so addition/subtraction naturally wrap at 32 bits.
                e.value = a + d.imm;
                break;
            case Op::SLTI:
                e.value = signed_value(a) < signed_value(d.imm);
                break;
            case Op::SLTIU:
                // The immediate is already sign-extended, then interpreted as unsigned.
                // For example -1 becomes 0xffffffff, rather than the 12-bit value 4095.
                e.value = a < d.imm;
                break;
            case Op::XORI:
                e.value = a ^ d.imm;
                break;
            case Op::ORI:
                e.value = a | d.imm;
                break;
            case Op::ANDI:
                e.value = a & d.imm;
                break;
            case Op::SLLI:
                e.value = a << d.imm;
                break;
            case Op::SRLI:
                e.value = a >> d.imm;
                break;
            case Op::SRAI:
                e.value = asr(a, d.imm);
                break;

            case Op::ADD:
                e.value = a + b;
                break;
            case Op::SUB:
                e.value = a - b;
                break;
            case Op::SLL:
                // RV32 register shifts use only rs2[4:0]; a count of 33 means shift by 1.
                e.value = a << (b & 31);
                break;
            case Op::SLT:
                e.value = signed_value(a) < signed_value(b);
                break;
            case Op::SLTU:
                e.value = a < b;
                break;
            case Op::XOR:
                e.value = a ^ b;
                break;
            case Op::SRL:
                e.value = a >> (b & 31);
                break;
            case Op::SRA:
                e.value = asr(a, b);
                break;
            case Op::OR:
                e.value = a | b;
                break;
            case Op::AND:
                e.value = a & b;
                break;

            case Op::FENCE:
                // Our single-hart RAM has no pending device/cache ordering to enforce.
                break;

            case Op::ECALL:
                // No host I/O/syscalls happen here. Both models defer termination to commit.
                e.trap = Trap{Fault::Ecall, pc, 0};
                break;
            case Op::EBREAK:
                e.trap = Trap{Fault::Ebreak, pc, 0};
                break;
            case Op::Illegal:
                e.trap = Trap{Fault::IllegalInstruction, pc, word};
                break;
        }

        if (d.op >= Op::BEQ && d.op <= Op::BGEU && e.redirect) {
            // A decoded branch immediate is a BYTE displacement, not an instruction count.
            e.next_pc = pc + d.imm;
        }

        if (e.redirect && (e.next_pc & 3)) {
            // Check only actual redirects: a not-taken branch cannot fault on its unused target.
            // The fault belongs to this branch/jump, not to a later fetch at the bad address.
            e.trap = Trap{Fault::InstructionAlignment, pc, e.next_pc};
        }

        return e;
    }

    std::string fault_name(Fault f) {
        static constexpr const char* names[] = {"instruction_alignment", "instruction_access",
            "illegal_instruction", "load_alignment", "load_access", "store_alignment",
            "store_access", "ecall", "ebreak"};

        return names[static_cast<unsigned>(f)];
    }
} // namespace rv
