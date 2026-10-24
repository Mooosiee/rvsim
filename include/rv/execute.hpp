#pragma once

#include "rv/decode.hpp"

namespace rv {
    // Proposed effects, not applied changes. Both execution models use this result.
    // EX calculates it, MEM supplies load data, and commit() makes effects permanent.
    struct Effect {
        Word value = 0;      // Candidate value for rd; for a load this is filled in MEM.
        Address address = 0; // Guest byte address for a load/store.
        Word store_value =
            0; // Value read from rs2, to be stored later if this instruction commits.

        Address next_pc = 0;   // Sequential PC + 4, or the resolved jump/taken-branch target.
        bool redirect = false; // EX must discard younger work and redirect speculative fetch.
        std::optional<Trap> trap;
    };

    // a/b contain the values of rs1/rs2, with pipeline forwarding already applied.
    // This function never reads or writes RegisterFile/Memory; it only computes an Effect.
    Effect execute(const Decoded& d, Address pc, Word word, Word a, Word b);
} // namespace rv
