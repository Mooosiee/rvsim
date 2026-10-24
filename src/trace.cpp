#include "rv/trace.hpp"
#include <iomanip>
#include <sstream>

namespace rv {
    std::string hex(Word value) {
        std::ostringstream out;
        out << "0x" << std::hex << std::setfill('0') << std::setw(8) << value;
        return out.str();
    }

    void write_registers(std::ostream& out, const RegisterFile& regs) {
        // One bit pattern can mean an unsigned number or a negative signed number.
        // Show both interpretations; printing a signed value does not change the register.
        for (unsigned i = 0; i < 32; ++i) {
            out << 'x' << i << " = " << hex(regs.read(i)) << " unsigned=" << regs.read(i)
                << " signed=" << signed_value(regs.read(i)) << '\n';
        }
    }

    void write_retirement(std::ostream& out, const Retirement& e, bool json) {
        // Commit events are directly comparable across models; cycle events are separate
        // because speculation/stalls change scheduling without changing the program's result.
        if (json) {
            out << "{\"type\":\"retire\",\"sequence\":" << e.sequence << ",\"pc\":" << e.pc
                << ",\"word\":" << e.word << ",\"op\":\"" << op_name(e.op)
                << "\",\"register_write\":";

            if (e.reg) {
                out << "{\"register\":" << e.reg->reg << ",\"value\":" << e.reg->value << '}';
            } else {
                out << "null";
            }
            out << ",\"memory_write\":";

            if (e.memory) {
                out << "{\"address\":" << e.memory->address << ",\"width\":" << e.memory->width
                    << ",\"value\":" << e.memory->value << '}';
            } else {
                out << "null";
            }
            out << ",\"next_pc\":" << e.next_pc << "}\n";
        } else {
            out << "retire #" << e.sequence << " pc=" << hex(e.pc) << " word=" << hex(e.word) << ' '
                << op_name(e.op);
            if (e.reg) {
                out << " x" << e.reg->reg << '=' << hex(e.reg->value);
            }
            if (e.memory) {
                out << " mem[" << hex(e.memory->address) << ":" << e.memory->width
                    << "]=" << hex(e.memory->value);
            }
            out << " next=" << hex(e.next_pc) << '\n';
        }
    }

    void write_trap(std::ostream& out, const Trap& e, bool json) {
        if (json) {
            out << "{\"type\":\"trap\",\"cause\":\"" << fault_name(e.cause) << "\",\"pc\":" << e.pc
                << ",\"value\":" << e.value << "}\n";
        } else {
            out << "trap " << fault_name(e.cause) << " pc=" << hex(e.pc)
                << " value=" << hex(e.value) << '\n';
        }
    }

    void write_cycle(std::ostream& out, const CycleEvent& e, bool json) {
        static constexpr const char* names[] = {"IF", "ID", "EX", "MEM", "WB"};

        if (json) {
            out << "{\"type\":\"cycle\",\"cycle\":" << e.cycle << ",\"stages\":{";
            for (unsigned i = 0; i < 5; ++i) {
                if (i) {
                    out << ',';
                }
                out << '"' << names[i] << "\":";
                if (e.stages[i]) {
                    out << "{\"tag\":" << e.stages[i]->tag << ",\"pc\":" << e.stages[i]->pc << '}';
                } else {
                    out << "null";
                }
            }
            out << "},\"stall\":\"" << e.stall << "\",\"forwarding\":[";

            for (std::size_t i = 0; i < e.forwarding.size(); ++i) {
                if (i) {
                    out << ',';
                }
                out << '"' << e.forwarding[i] << '"';
            }
            out << "],\"redirect\":" << (e.redirect ? "true" : "false")
                << ",\"target\":" << e.target << ",\"flushed\":" << e.flushed << "}\n";
        } else {
            out << "cycle " << e.cycle;
            for (unsigned i = 0; i < 5; ++i) {
                out << ' ' << names[i] << '=';
                if (e.stages[i]) {
                    out << hex(e.stages[i]->pc);
                } else {
                    out << '-';
                }
            }
            if (!e.stall.empty()) {
                out << " stall=" << e.stall;
            }
            for (const auto& f : e.forwarding) {
                out << " forward=" << f;
            }
            if (e.redirect) {
                out << " redirect=" << hex(e.target);
            }
            if (e.flushed) {
                out << " flushed=" << e.flushed;
            }
            out << '\n';
        }
    }

    void write_stats(std::ostream& out, const Machine& m, const std::string& status) {
        const auto& s = m.statistics();

        out << "{\n  \"status\":\"" << status << "\",\n  \"model\":\""
            << (m.model() == Model::Pipeline ? "pipeline" : "functional")
            << "\",\n  \"retired\":" << s.retired << ",\n  \"cycles\":";
        if (m.model() == Model::Pipeline) {
            out << s.cycles;
        } else {
            out << "null";
        }
        out << ",\n  \"cpi\":";

        if (m.model() == Model::Pipeline && s.retired) {
            out << std::setprecision(10) << double(s.cycles) / double(s.retired);
        } else {
            out << "null";
        }
        out << ",\n  \"stalls\":{\"load_use\":" << s.load_use_stalls << ",\"raw\":" << s.raw_stalls
            << "},\n  \"redirects\":" << s.redirects << ",\n  \"flushed\":" << s.flushed
            << ",\n  \"instruction_mix\":{";

        bool first = true;
        for (const auto& item : s.mix) {
            if (!first) {
                out << ',';
            }
            first = false;
            out << '"' << item.first << "\":" << item.second;
        }
        out << "},\n  \"pc\":" << m.pc() << ",\n  \"registers\":[";

        for (unsigned i = 0; i < 32; ++i) {
            if (i) {
                out << ',';
            }
            out << m.registers().read(i);
        }
        out << "],\n  \"memory_nonzero_bytes\":{";

        first = true;
        const auto& bytes = m.memory().bytes();
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i]) {
                if (!first) {
                    out << ',';
                }
                first = false;
                out << '"' << i << "\":" << unsigned(bytes[i]);
            }
        }
        out << "},\n  \"trap\":";

        if (m.terminal()) {
            out << "{\"cause\":\"" << fault_name(m.terminal()->cause)
                << "\",\"pc\":" << m.terminal()->pc << ",\"value\":" << m.terminal()->value << '}';
        } else {
            out << "null";
        }
        out << "\n}\n";
    }
} // namespace rv
