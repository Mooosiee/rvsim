#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace rv {
    // RV32 means a guest register holds 32 bits, even on a 64-bit host computer.
    // Unsigned arithmetic gives the modulo-2^32 wraparound required by RV32I.
    using Word = std::uint32_t;
    // Addresses count guest bytes; they are indices into Memory, not host pointers.
    using Address = std::uint32_t;

    // Avoid implementation-defined conversion of large unsigned values to int32_t.
    inline std::int64_t signed_value(Word v) {
        return v < 0x80000000u ? std::int64_t(v) : std::int64_t(v) - 0x100000000ll;
    }

    // Sign extension keeps a smaller signed value's meaning in a 32-bit register.
    // Example: a 12-bit immediate 0xfff represents -1, so it becomes 0xffffffff.
    inline Word sext(Word value, unsigned bits) {
        const Word sign = Word(1) << (bits - 1);
        return (value ^ sign) - sign;
    }

    enum class Fault {
        InstructionAlignment,
        InstructionAccess,
        IllegalInstruction,
        LoadAlignment,
        LoadAccess,
        StoreAlignment,
        StoreAccess,
        Ecall,
        Ebreak
    };

    std::string fault_name(Fault f);

    // A guest fault/completion record travels with its instruction until commit.
    // value contains the bad address or illegal word; pc identifies the instruction.
    struct Trap {
        Fault cause;
        Address pc = 0;
        Word value = 0;
    };

    // Internal C++ exception for a failed access. Machine converts it into a guest Trap.
    class MemoryError : public std::runtime_error {
    public:
        Fault cause;
        Address address;

        MemoryError(Fault c, Address a)
            : std::runtime_error("guest memory access"), cause(c), address(a) {}
    };

    // The 32 integer registers are named x0..x31. ABI names are aliases, not extra
    // registers: zero=x0, ra=x1, sp=x2, t0=x5, a0=x10 (usual first argument/result).
    // See docs/INSTRUCTION_REFERENCE.md for the complete register/ABI table.
    class RegisterFile {
        std::array<Word, 32> values_{};

    public:
        Word read(unsigned index) const {
            return values_.at(index);
        }

        void write(unsigned index, Word value) {
            // x0 is hardwired to zero. Instructions may name it as a destination
            // to discard a result, but their other behavior (such as load faults) remains.
            if (index != 0) {
                values_.at(index) = value;
            }
        }

        void reset() {
            values_.fill(0);
        }

        const std::array<Word, 32>& values() const {
            return values_;
        }
    };
} // namespace rv
