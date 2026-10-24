#pragma once

#include "rv/machine.hpp"
#include <ostream>

namespace rv {
    // Output only: these functions serialize state/events without advancing the CPU.
    // A retire record describes committed effects; a cycle record includes speculation.
    std::string hex(Word value);
    void write_retirement(std::ostream& out, const Retirement& e, bool json);
    void write_trap(std::ostream& out, const Trap& e, bool json);
    void write_cycle(std::ostream& out, const CycleEvent& e, bool json);
    void write_stats(std::ostream& out, const Machine& machine, const std::string& status);
    void write_registers(std::ostream& out, const RegisterFile& regs);
} // namespace rv
