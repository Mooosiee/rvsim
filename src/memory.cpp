#include "rv/memory.hpp"
#include <algorithm>
#include <limits>

namespace rv {
    Memory::Memory(std::size_t size) : bytes_(size, 0) {
        if (!size || std::uint64_t(size) > (std::uint64_t(1) << 32)) {
            throw std::invalid_argument("memory size must be in [1, 2^32]");
        }
    }

    void Memory::validate(Address a, unsigned width, bool store, bool fetch) const {
        if (width != 1 && width != 2 && width != 4) {
            throw std::invalid_argument("invalid access width");
        }

        const auto alignment = fetch   ? Fault::InstructionAlignment
                               : store ? Fault::StoreAlignment
                                       : Fault::LoadAlignment;
        const auto access = fetch   ? Fault::InstructionAccess
                            : store ? Fault::StoreAccess
                                    : Fault::LoadAccess;

        if (a % width) {
            // Natural alignment: bytes anywhere, halfwords at multiples of 2, words of 4.
            throw MemoryError(alignment, a);
        }
        if (std::uint64_t(a) + width > bytes_.size()) {
            // Widen before adding so a near-0xffffffff address cannot wrap into valid RAM.
            throw MemoryError(access, a);
        }
    }

    Word Memory::read(Address a, unsigned width, bool fetch) const {
        validate(a, width, false, fetch);

        Word result = 0;
        // Little-endian: address a holds bits 7..0, a+1 holds bits 15..8, and so on.
        for (unsigned i = 0; i < width; ++i) {
            result |= Word(bytes_[std::size_t(a) + i]) << (8 * i);
        }

        return result;
    }

    void Memory::write(Address a, unsigned width, Word value) {
        // Checking the whole access first makes a failed store atomic: no partial bytes change.
        validate(a, width, true);

        for (unsigned i = 0; i < width; ++i) {
            bytes_[std::size_t(a) + i] = std::uint8_t(value >> (8 * i));
        }
    }

    void Memory::initialize(Address a, const std::vector<std::uint8_t>& bytes) {
        // Image segments may contain any byte count; guest instruction alignment is
        // checked at execution time, not imposed on this loader operation.
        if (std::uint64_t(a) + bytes.size() > bytes_.size()) {
            throw std::invalid_argument("image outside RAM");
        }

        std::copy(bytes.begin(), bytes.end(), bytes_.begin() + a);
    }
} // namespace rv
