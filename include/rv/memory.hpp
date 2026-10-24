#pragma once

#include "rv/types.hpp"
#include <vector>

namespace rv {
    // Guest RAM owns bytes. Guest addresses are never cast to host pointers.
    class Memory {
        std::vector<std::uint8_t> bytes_;

    public:
        explicit Memory(std::size_t size = 65536);

        std::size_t size() const {
            return bytes_.size();
        }

        // Check natural alignment and the whole range before accessing any bytes.
        // fetch selects instruction-access faults; store selects store-access faults.
        void validate(Address address, unsigned width, bool store, bool fetch = false) const;
        Word read(Address address, unsigned width, bool fetch = false) const;
        void write(Address address, unsigned width, Word value);
        // Loader copy: arbitrary byte ranges are allowed; this is not a guest SW instruction.
        void initialize(Address address, const std::vector<std::uint8_t>& bytes);

        const std::vector<std::uint8_t>& bytes() const {
            return bytes_;
        }
    };
} // namespace rv
