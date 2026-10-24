#include "rv/loader.hpp"
#include "rv/trace.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>

namespace {
    using namespace rv;

    struct Options {
        Model model = Model::Functional;
        std::string program, format = "elf", trace, trace_format = "text", stats;
        std::size_t memory_size = 65536;
        Address load = 0x1000, entry = 0x1000;
        std::uint64_t max_instructions = 1000000, max_cycles = 10000000;
        bool forwarding = true, debug = false;
    };

    const char* help = R"(rvsim --program PATH --format elf|bin [options]
  --model functional|pipeline       default functional
  --memory-size BYTES               zero-based RAM, default 65536
  --load-address ADDRESS --entry ADDRESS  bin only, default 0x1000
  --max-instructions N              committed normal instructions, default 1000000
  --max-cycles N                    pipeline only, default 10000000
  --forwarding on|off               pipeline only, default on
  --trace PATH --trace-format text|jsonl
  --stats PATH                     JSON counters and final architectural state
  --debug                          interactive debugger
  --help
Numbers accept decimal or 0x hex. EBREAK at commit completes normally.
Exit: 0 completion/debug quit, 1 simulation fault, 2 limit, 64 usage/input error.
)";

    std::uint64_t number(const std::string& s) {
        if (s.empty() || s[0] == '-' || s[0] == '+' ||
            s.find_first_of(" \t\r\n") != std::string::npos) {
            throw std::invalid_argument("invalid unsigned number: " + s);
        }

        std::size_t used = 0;
        const auto v = std::stoull(s, &used, s.size() > 2 && s.substr(0, 2) == "0x" ? 16 : 10);
        if (used != s.size()) {
            throw std::invalid_argument("invalid number: " + s);
        }

        return v;
    }

    Address address(const std::string& s) {
        const auto v = number(s);
        if (v > 0xffffffffull) {
            throw std::invalid_argument("address exceeds 32 bits");
        }

        return Address(v);
    }

    Options parse(int argc, char** argv) {
        Options o;
        std::set<std::string> seen;

        for (int i = 1; i < argc; ++i) {
            const std::string k = argv[i];

            if (!seen.insert(k).second) {
                throw std::invalid_argument("duplicate option: " + k);
            }
            if (k == "--debug") {
                o.debug = true;
                continue;
            }
            if (i + 1 == argc) {
                throw std::invalid_argument("missing value: " + k);
            }

            const std::string v = argv[++i];

            if (k == "--model") {
                if (v != "functional" && v != "pipeline") {
                    throw std::invalid_argument("invalid model");
                }
                o.model = v == "pipeline" ? Model::Pipeline : Model::Functional;
            } else if (k == "--program") {
                o.program = v;
            } else if (k == "--format") {
                o.format = v;
            } else if (k == "--memory-size") {
                const auto n = number(v);
                if (!n || n > 0x100000000ull || n > std::numeric_limits<std::size_t>::max()) {
                    throw std::invalid_argument("invalid memory size");
                }
                o.memory_size = std::size_t(n);
            } else if (k == "--load-address") {
                o.load = address(v);
            } else if (k == "--entry") {
                o.entry = address(v);
            } else if (k == "--max-instructions") {
                o.max_instructions = number(v);
            } else if (k == "--max-cycles") {
                o.max_cycles = number(v);
            } else if (k == "--forwarding") {
                if (v != "on" && v != "off") {
                    throw std::invalid_argument("forwarding must be on or off");
                }
                o.forwarding = v == "on";
            } else if (k == "--trace") {
                o.trace = v;
            } else if (k == "--trace-format") {
                o.trace_format = v;
            } else if (k == "--stats") {
                o.stats = v;
            } else {
                throw std::invalid_argument("unknown option: " + k);
            }
        }

        if (o.program.empty()) {
            throw std::invalid_argument("--program is required");
        }
        if (o.format != "elf" && o.format != "bin") {
            throw std::invalid_argument("format must be elf or bin");
        }
        if (o.format == "elf" && (seen.count("--entry") || seen.count("--load-address"))) {
            throw std::invalid_argument("raw addresses are irrelevant to ELF");
        }
        if (o.format == "bin" && !seen.count("--entry")) {
            o.entry = o.load;
        }

        if (o.model == Model::Functional &&
            (seen.count("--forwarding") || seen.count("--max-cycles"))) {
            throw std::invalid_argument("forwarding/max-cycles require pipeline model");
        }
        if (o.trace_format != "text" && o.trace_format != "jsonl") {
            throw std::invalid_argument("invalid trace format");
        }
        if (seen.count("--trace-format") && o.trace.empty()) {
            throw std::invalid_argument("trace-format requires trace");
        }

        auto same = [](const std::string& a, const std::string& b) {
            if (a.empty() || b.empty()) {
                return false;
            }

            std::error_code ec;
            if (std::filesystem::equivalent(a, b, ec)) {
                return true;
            }

            auto left = std::filesystem::absolute(a).lexically_normal().string();
            auto right = std::filesystem::absolute(b).lexically_normal().string();
#ifdef _WIN32
            auto lower = [](unsigned char c) {
                return char(std::tolower(c));
            };
            std::transform(left.begin(), left.end(), left.begin(), lower);
            std::transform(right.begin(), right.end(), right.begin(), lower);
#endif

            return left == right;
        };

        if (same(o.program, o.trace) || same(o.program, o.stats) || same(o.trace, o.stats)) {
            throw std::invalid_argument("program/trace/stats paths must differ");
        }

        return o;
    }

    // Host-side execution/debug loop. Machine implements the CPU; this wrapper handles
    // budgets, user commands, and whether execution should pause before the next tick.
    class Session {
        Machine& m_;
        const Options& o_;
        Memory initial_;
        Address entry_;
        std::set<Address> breakpoints_;
        bool stopped_at_breakpoint_ = false;

    public:
        std::string status = "running";

        Session(Machine& m, const Options& o, Address entry)
            : m_(m), o_(o), initial_(m.memory()), entry_(entry) {}

        bool advance(bool ignore_breakpoint = false) {
            if (m_.halted()) {
                return false;
            }

            if (m_.statistics().retired >= o_.max_instructions ||
                (m_.model() == Model::Pipeline && m_.statistics().cycles >= o_.max_cycles)) {
                status = "limit";
                return false;
            }

            const auto next = m_.next_commit_pc();
            // Breakpoints are on architectural instructions, not on speculative fetches.
            // This check happens before tick(), so the targeted store/register write is untouched.
            if (!ignore_breakpoint && next && breakpoints_.count(*next)) {
                std::cout << "breakpoint before commit pc=" << hex(*next) << '\n';
                stopped_at_breakpoint_ = true;
                return false;
            }

            m_.tick();
            if (m_.halted()) {
                status = m_.terminal()->cause == Fault::Ebreak ? "completed" : "fault";
            }

            return !m_.halted();
        }

        void run() {
            while (advance()) {
            }
        }

        void debug() {
            std::cout << "Debugger: help for commands. Breakpoints stop before commit.\n";
            std::string line;

            while (std::cout << "rvsim> " && std::getline(std::cin, line)) {
                std::istringstream in(line);
                std::string cmd, a, b, extra;
                in >> cmd >> a >> b >> extra;

                try {
                    if (cmd.empty()) {
                        continue;
                    }
                    if (!extra.empty()) {
                        throw std::invalid_argument("too many arguments");
                    }

                    if (cmd == "help") {
                        std::cout
                            << "help | continue | registers | memory ADDRESS LENGTH | breakpoint "
                               "ADDRESS | delete ADDRESS | quit\nstep (next retirement/trap) | "
                               "tick (pipeline cycle) | stages (pipeline) | reset\n";
                    } else if (cmd == "quit") {
                        status = "quit";
                        return;
                    } else if (cmd == "registers") {
                        write_registers(std::cout, m_.registers());
                    } else if (cmd == "memory") {
                        const auto start = address(a);
                        const auto length = number(b);
                        if (std::uint64_t(start) + length > m_.memory().size()) {
                            throw std::invalid_argument("memory range outside RAM");
                        }

                        for (std::uint64_t i = 0; i < length; ++i) {
                            std::cout << hex(Address(start + i)) << ": "
                                      << hex(m_.memory().read(Address(start + i), 1)) << '\n';
                        }
                    } else if (cmd == "breakpoint" || cmd == "delete") {
                        if (!b.empty()) {
                            throw std::invalid_argument("expected one address");
                        }

                        const auto pc = address(a);
                        if (pc & 3) {
                            throw std::invalid_argument("breakpoint must be aligned");
                        }

                        if (cmd == "breakpoint") {
                            breakpoints_.insert(pc);
                        } else {
                            breakpoints_.erase(pc);
                        }
                    } else if (cmd == "stages") {
                        if (m_.model() != Model::Pipeline) {
                            throw std::invalid_argument("stages requires pipeline");
                        }

                        const auto& p = m_.pipeline();
                        const Packet* slots[] = {&p.if_id, &p.id_ex, &p.ex_mem, &p.mem_wb};
                        const char* names[] = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};

                        for (unsigned i = 0; i < 4; ++i) {
                            std::cout << names[i] << ' ';
                            if (slots[i]->valid) {
                                std::cout << hex(slots[i]->pc);
                            } else {
                                std::cout << "empty";
                            }
                            std::cout << '\n';
                        }
                    } else if (cmd == "reset") {
                        m_.memory() = initial_;
                        m_.reset(entry_);
                        status = "running";
                        stopped_at_breakpoint_ = false;

                        std::cout << "initial RAM, registers, entry, counters restored; "
                                     "breakpoints preserved\n";
                    } else if (cmd == "continue" || cmd == "step" || cmd == "tick") {
                        if (cmd == "tick" && m_.model() != Model::Pipeline) {
                            throw std::invalid_argument("tick requires pipeline");
                        }
                        if (status == "limit" || m_.halted()) {
                            std::cout << "stopped: " << status << "; reset to restart\n";
                            continue;
                        }

                        const auto before = m_.statistics().retired;
                        const bool skip = stopped_at_breakpoint_;
                        stopped_at_breakpoint_ = false;

                        bool more = advance(skip);
                        if (cmd == "continue") {
                            while (more) {
                                more = advance();
                            }
                        }

                        if (cmd == "step") {
                            // In a pipeline, several ticks may pass before any instruction retires.
                            // Stop at that architectural boundary, rather than after one clock
                            // tick.
                            while (more && m_.statistics().retired == before) {
                                more = advance();
                            }
                        }

                        std::cout << "pc=" << hex(m_.pc()) << " retired=" << m_.statistics().retired
                                  << " status=" << status << '\n';
                    } else {
                        throw std::invalid_argument("unknown debugger command");
                    }
                } catch (const std::exception& e) {
                    std::cout << "debug error: " << e.what() << '\n';
                }
            }

            if (status == "running") {
                status = "quit";
            }
        }
    };
} // namespace

int main(int argc, char** argv) {
    using namespace rv;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            std::cout << help;
            return 0;
        }
    }

    try {
        // Startup: parse host options, allocate zeroed guest RAM, load its bytes,
        // and reset registers/PC to the image's entry. Guest startup sets its own stack.
        const auto o = parse(argc, argv);
        Machine m(o.model, o.memory_size, o.forwarding);
        const auto bytes = read_file(o.program);
        const auto image = o.format == "elf" ? load_elf(m.memory(), bytes)
                                             : load_raw(m.memory(), bytes, o.load, o.entry);
        m.reset(image.entry);

        std::ofstream trace;
        if (!o.trace.empty()) {
            trace.open(o.trace);
            if (!trace) {
                throw std::invalid_argument("cannot open trace output");
            }
        }

        const bool json = o.trace_format == "jsonl";
        // Observers serialize events the CPU has already decided. They do not implement
        // instruction effects, so text/JSON output cannot change simulation ordering.
        m.on_retire = [&](const Retirement& e) {
            if (trace.is_open()) {
                write_retirement(trace, e, json);
            }
        };
        m.on_cycle = [&](const CycleEvent& e) {
            if (trace.is_open()) {
                write_cycle(trace, e, json);
            }
        };
        m.on_trap = [&](const Trap& e) {
            if (trace.is_open()) {
                write_trap(trace, e, json);
            }
        };

        Session session(m, o, image.entry);
        if (o.debug) {
            session.debug();
        } else {
            session.run();
        }

        if (trace.is_open()) {
            trace.flush();
            if (!trace) {
                throw std::runtime_error("trace write failed");
            }
        }

        std::cout << session.status << " pc=" << hex(m.pc())
                  << " retired=" << m.statistics().retired;
        if (o.model == Model::Pipeline) {
            std::cout << " cycles=" << m.statistics().cycles;
        }
        std::cout << '\n';
        if (m.terminal()) {
            write_trap(std::cout, *m.terminal(), false);
        }
        std::cout << "a0 = " << hex(m.registers().read(10))
                  << " unsigned=" << m.registers().read(10)
                  << " signed=" << signed_value(m.registers().read(10)) << '\n';

        if (!o.stats.empty()) {
            std::ofstream stats(o.stats);
            if (!stats) {
                throw std::invalid_argument("cannot open stats output");
            }

            write_stats(stats, m, session.status);
            stats.flush();
            if (!stats) {
                throw std::runtime_error("stats write failed");
            }
        }

        if (session.status == "fault") {
            return 1;
        }
        if (session.status == "limit") {
            return 2;
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "rvsim: " << e.what() << '\n';
        return 64;
    }
}
