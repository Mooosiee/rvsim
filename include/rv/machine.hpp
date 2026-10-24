#pragma once

#include "rv/execute.hpp"
#include "rv/memory.hpp"
#include <functional>
#include <map>
#include <vector>

namespace rv {
    enum class Model {
        Functional,
        Pipeline
    };

    struct RegisterWrite {
        unsigned reg;
        Word value;
    };

    struct MemoryWrite {
        Address address;
        unsigned width;
        Word value;
    };

    // One instruction has permanently finished. Optional writes describe exactly what
    // changed; a branch can retire without writing a register or memory location.
    struct Retirement {
        std::uint64_t sequence;
        Address pc;
        Word word;
        Op op;

        std::optional<RegisterWrite> reg;
        std::optional<MemoryWrite> memory;
        Address next_pc;
    };

    // One instruction's working state as it travels through the pipeline.
    struct Packet {
        bool valid = false;    // false is an empty stage/bubble, not an executed NOP.
        std::uint64_t tag = 0; // Speculative fetch identity; it can disappear on a flush.

        Address pc = 0;
        Word word = 0;

        Decoded decoded;
        Word a = 0, b = 0; // Source values captured in ID; EX may replace them via forwarding.
        Effect effect;
    };

    // Latches hold information between stages: IF -> ID -> EX -> MEM -> WB.
    // pipeline_tick() reads current latches and builds a separate next set each cycle.
    struct PipelineState {
        Packet if_id, id_ex, ex_mem, mem_wb;
    };

    struct Stage {
        std::uint64_t tag;
        Address pc;
    };

    struct CycleEvent {
        std::uint64_t cycle = 0;

        // Occupancy at cycle start; IF is the attempted fetch in this cycle.
        std::array<std::optional<Stage>, 5> stages{};

        std::string stall;
        std::vector<std::string> forwarding;

        bool redirect = false;
        Address target = 0;
        std::uint64_t flushed = 0;
    };

    struct Statistics {
        std::uint64_t retired = 0, cycles = 0, load_use_stalls = 0, raw_stalls = 0, redirects = 0,
                      flushed = 0;
        std::map<std::string, std::uint64_t> mix;
    };

    // Owns guest architecture and scheduling. The CLI calls tick(); instruction
    // semantics live in execute(), while permanent effects are centralized in commit().
    class Machine {
        Model model_;
        bool forwarding_;

        RegisterFile regs_;
        Memory memory_;

        // pc_: next committed architectural instruction; fetch_pc_: speculative front end.
        // A debugger/result comparison uses pc_, never the younger fetch position.
        Address pc_ = 0, fetch_pc_ = 0;
        std::uint64_t fetch_tag_ = 0;

        PipelineState current_;
        // front_stopped_ drains a possible trap; halted_ means it actually committed.
        // An older taken branch can cancel a speculative trap and reopen the front end.
        bool front_stopped_ = false, halted_ = false;
        std::optional<Trap> terminal_;
        Statistics stats_;

        Packet fetch(Address pc);
        void memory_access(Packet& p);
        void commit(const Packet& p);
        void functional_tick();
        void pipeline_tick();

    public:
        explicit Machine(
            Model model = Model::Functional, std::size_t size = 65536, bool forwarding = true)
            : model_(model), forwarding_(forwarding), memory_(size) {}

        // Reset clears registers, scheduler, counters, and terminal state; loaded RAM is preserved.
        void reset(Address entry);
        // Functional: one instruction/trap. Pipeline: one simulated clock cycle.
        void tick();

        Model model() const {
            return model_;
        }

        RegisterFile& registers() {
            return regs_;
        }

        const RegisterFile& registers() const {
            return regs_;
        }

        Memory& memory() {
            return memory_;
        }

        const Memory& memory() const {
            return memory_;
        }

        Address pc() const {
            return pc_;
        }

        bool halted() const {
            return halted_;
        }

        const std::optional<Trap>& terminal() const {
            return terminal_;
        }

        const Statistics& statistics() const {
            return stats_;
        }

        const PipelineState& pipeline() const {
            return current_;
        }

        // Used for breakpoints: stop before the oldest instruction applies its effects.
        std::optional<Address> next_commit_pc() const;

        // Callbacks let traces/debugging observe events without implementing CPU semantics.
        std::function<void(const Retirement&)> on_retire;
        std::function<void(const Trap&)> on_trap;
        std::function<void(const CycleEvent&)> on_cycle;
    };
} // namespace rv
