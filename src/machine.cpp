#include "rv/machine.hpp"

namespace rv {
    void Machine::reset(Address entry) {
        regs_.reset();
        pc_ = fetch_pc_ = entry;
        fetch_tag_ = 0;

        current_ = {};
        front_stopped_ = halted_ = false;
        terminal_.reset();
        stats_ = {};
    }

    Packet Machine::fetch(Address pc) {
        // Instruction fetch can fail before we have a word to decode. Attach the fault
        // to its PC rather than terminating immediately: this fetch might be wrong-path.
        Packet p;
        p.valid = true;
        p.pc = pc;
        p.tag = ++fetch_tag_;

        try {
            p.word = memory_.read(pc, 4, true);
        } catch (const MemoryError& err) {
            p.effect.trap = Trap{err.cause, pc, err.address};
        }

        return p;
    }

    void Machine::memory_access(Packet& p) {
        if (!p.valid || p.effect.trap) {
            return;
        }

        const auto& d = p.decoded;

        try {
            if (d.load) {
                p.effect.value = memory_.read(p.effect.address, d.width);

                if (d.op == Op::LB) {
                    // LB/LH preserve a negative memory value when widening to a full register.
                    // LBU/LHU leave the upper bits zero because Memory::read starts from zero.
                    p.effect.value = sext(p.effect.value, 8);
                }
                if (d.op == Op::LH) {
                    p.effect.value = sext(p.effect.value, 16);
                }
            }

            if (d.store) {
                // Validate now, but keep bytes unchanged until this store reaches WB.
                memory_.validate(p.effect.address, d.width, true);
            }
        } catch (const MemoryError& err) {
            p.effect.trap = Trap{err.cause, p.pc, err.address};
        }
    }

    void Machine::commit(const Packet& p) {
        // This is the architectural boundary: the only place a running instruction
        // changes registers, stores bytes, updates architectural PC, or terminates.
        if (!p.valid) {
            return;
        }

        if (p.effect.trap) {
            // Older instructions have already committed. The faulting instruction has
            // no write effects, and the caller suppresses younger pipeline work.
            terminal_ = p.effect.trap;
            pc_ = p.pc;
            halted_ = true;

            if (on_trap) {
                on_trap(*terminal_);
            }

            return;
        }

        Retirement event{stats_.retired + 1, p.pc, p.word, p.decoded.op, {}, {}, p.effect.next_pc};

        if (p.decoded.store) {
            const auto width = p.decoded.width;
            const auto mask = width == 4 ? ~Word(0) : (Word(1) << (width * 8)) - 1;
            memory_.write(p.effect.address, width, p.effect.store_value);
            event.memory = MemoryWrite{p.effect.address, width, p.effect.store_value & mask};
        }

        if (p.decoded.writes_rd && p.decoded.rd != 0) {
            regs_.write(p.decoded.rd, p.effect.value);
            event.reg = RegisterWrite{p.decoded.rd, p.effect.value};
        }

        pc_ = p.effect.next_pc;
        ++stats_.retired;
        ++stats_.mix[op_name(p.decoded.op)];

        if (on_retire) {
            on_retire(event);
        }
    }

    void Machine::functional_tick() {
        // Read this path first: one instruction runs through fetch, decode, EX, MEM,
        // then commit. Pipeline mode schedules the same semantics over several ticks.
        Packet p = fetch(pc_);

        if (!p.effect.trap) {
            p.decoded = decode(p.word);
            p.effect = execute(
                p.decoded, p.pc, p.word, regs_.read(p.decoded.rs1), regs_.read(p.decoded.rs2));
            memory_access(p);
        }

        commit(p);
    }

    static bool producer(const Packet& p, unsigned reg) {
        // A true dependency needs a real writing instruction and a nonzero destination.
        // x0 writes are discarded, so waiting for or forwarding them would be incorrect.
        return reg != 0 && p.valid && !p.effect.trap && p.decoded.writes_rd && p.decoded.rd == reg;
    }

    void Machine::pipeline_tick() {
        // All stage inputs come from this immutable snapshot. Only WB mutates architecture.
        // Empty 'next' packets are bubbles unless a stage explicitly puts work into them.
        const PipelineState old = current_;
        PipelineState next;

        CycleEvent event;
        event.cycle = ++stats_.cycles;

        const Packet* slots[] = {nullptr, &old.if_id, &old.id_ex, &old.ex_mem, &old.mem_wb};
        for (unsigned i = 1; i < 5; ++i) {
            if (slots[i]->valid) {
                event.stages[i] = Stage{slots[i]->tag, slots[i]->pc};
            }
        }

        // WB first: same-cycle ID reads observe writes; the immediately younger MEM load observes
        // stores.
        commit(old.mem_wb);

        if (halted_) {
            current_ = {};

            if (on_cycle) {
                on_cycle(event);
            }

            return;
        }

        next.mem_wb = old.ex_mem;
        memory_access(next.mem_wb);

        if (next.mem_wb.valid && next.mem_wb.effect.trap) {
            // A MEM fault is older than EX; suppress EX redirects and all younger work.
            front_stopped_ = true;
            event.flushed = std::uint64_t(old.id_ex.valid) + std::uint64_t(old.if_id.valid);
        } else {
            next.ex_mem = old.id_ex;

            if (next.ex_mem.valid && !next.ex_mem.effect.trap) {
                auto operand = [&](unsigned reg, Word latched, bool used, const char* role) {
                    if (!used || !reg || !forwarding_) {
                        return latched;
                    }

                    if (producer(old.ex_mem, reg) && !old.ex_mem.decoded.load) {
                        // This is the newer writer, so it wins over a matching MEM/WB writer.
                        // A load here has an address, not its data: never forward that address.
                        event.forwarding.push_back(
                            std::string(role) + ":x" + std::to_string(reg) + "<-EX/MEM");
                        return old.ex_mem.effect.value;
                    }

                    if (producer(old.mem_wb, reg)) {
                        // Loads have obtained their data by MEM/WB and can now be forwarded.
                        event.forwarding.push_back(
                            std::string(role) + ":x" + std::to_string(reg) + "<-MEM/WB");
                        return old.mem_wb.effect.value;
                    }

                    return latched;
                };

                const auto& d = next.ex_mem.decoded;
                // Both sources use the same selection rule. This covers ALU/branch inputs,
                // a JALR base, a store base (rs1), and store data (rs2).
                const Word a = operand(d.rs1, next.ex_mem.a, d.uses_rs1, "rs1");
                const Word b = operand(d.rs2, next.ex_mem.b, d.uses_rs2, "rs2");
                next.ex_mem.effect = execute(d, next.ex_mem.pc, next.ex_mem.word, a, b);
            }

            if (next.ex_mem.valid && next.ex_mem.effect.trap) {
                front_stopped_ = true;
                event.flushed = old.if_id.valid ? 1 : 0;
            } else if (next.ex_mem.valid && next.ex_mem.effect.redirect) {
                // The younger IF/ID packet is from the predicted sequential path.
                // Discard it even if it contains an EBREAK/illegal/fetch-fault packet.
                fetch_pc_ = next.ex_mem.effect.next_pc;
                front_stopped_ = false;

                event.redirect = true;
                event.target = fetch_pc_;
                event.flushed = old.if_id.valid ? 1 : 0;
                ++stats_.redirects;
                // No fetch on the redirect cycle. ID and IF slots are lost: two-cycle penalty.
            } else {
                Packet decoded = old.if_id;
                bool stall = false;

                if (decoded.valid && !decoded.effect.trap) {
                    decoded.decoded = decode(decoded.word);
                    const auto& d = decoded.decoded;

                    const auto depends = [&](const Packet& p) {
                        return (d.uses_rs1 && producer(p, d.rs1)) ||
                               (d.uses_rs2 && producer(p, d.rs2));
                    };

                    if (forwarding_) {
                        // A load in EX cannot feed the instruction about to enter EX next
                        // cycle: its data will not be in MEM/WB yet. One bubble fixes that.
                        stall = old.id_ex.decoded.load && depends(old.id_ex);
                        if (stall) {
                            event.stall = "load_use";
                            ++stats_.load_use_stalls;
                        }
                    } else {
                        // No bypasses: wait for ID/EX and EX/MEM writers to reach WB.
                        // MEM/WB needs no extra stall: this tick's WB already updated registers.
                        stall = depends(old.id_ex) || depends(old.ex_mem);
                        if (stall) {
                            event.stall = "raw";
                            ++stats_.raw_stalls;
                        }
                    }

                    if (!stall) {
                        // ID reads after WB in this tick. These captured values can still
                        // become stale before EX, which is why EX forwarding remains necessary.
                        decoded.a = regs_.read(d.rs1);
                        decoded.b = regs_.read(d.rs2);

                        if (d.op == Op::Illegal || d.op == Op::ECALL || d.op == Op::EBREAK) {
                            // Tagged faults remain speculative until WB, and can still be
                            // redirected away.
                            decoded.effect =
                                execute(d, decoded.pc, decoded.word, decoded.a, decoded.b);
                            front_stopped_ = true;
                        }
                    }
                }

                if (stall) {
                    // next.id_ex stays empty. Do not fetch or advance fetch_pc_ while
                    // this consumer waits; older EX/MEM/WB instructions still progress.
                    next.if_id =
                        old.if_id; // Hold PC and IF/ID, inject empty ID/EX. Older stages advance.
                } else {
                    next.id_ex = decoded;
                    if (!front_stopped_) {
                        next.if_id = fetch(fetch_pc_);
                        event.stages[0] = Stage{next.if_id.tag, fetch_pc_};
                        fetch_pc_ += 4;
                        if (next.if_id.effect.trap) {
                            front_stopped_ = true;
                        }
                    }
                }
            }
        }

        stats_.flushed += event.flushed;
        current_ = next; // The only latch update of this cycle.

        if (on_cycle) {
            on_cycle(event);
        }
    }

    void Machine::tick() {
        if (halted_) {
            return;
        }

        if (model_ == Model::Functional) {
            functional_tick();
        } else {
            pipeline_tick();
        }
    }

    std::optional<Address> Machine::next_commit_pc() const {
        // Checking speculative fetch/decode addresses would stop on wrong-path instructions.
        // The CLI calls this before tick(), so a breakpoint prevents even a WB store's effects.
        if (halted_) {
            return {};
        }

        if (model_ == Model::Functional) {
            return pc_;
        }

        if (current_.mem_wb.valid) {
            return current_.mem_wb.pc;
        }

        return {};
    }
} // namespace rv
