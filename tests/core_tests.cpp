#include "rv/loader.hpp"
#include "rv/machine.hpp"
#include "rv/trace.hpp"
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace rv;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(                                                              \
                std::string(__func__) + ":" + std::to_string(__LINE__) + " " #x);                  \
    } while (false)

template <class F> void rejects(F f) {
    bool caught = false;

    try {
        f();
    } catch (const std::exception&) {
        caught = true;
    }

    CHECK(caught);
}

Word imm(unsigned rd, unsigned rs, int value, unsigned f3 = 0, unsigned opcode = 0x13) {
    return (Word(value) & 0xfff) << 20 | rs << 15 | f3 << 12 | rd << 7 | opcode;
}

Word reg(unsigned rd, unsigned a, unsigned b, unsigned f3 = 0, unsigned f7 = 0) {
    return f7 << 25 | b << 20 | a << 15 | f3 << 12 | rd << 7 | 0x33;
}

Word store(unsigned a, unsigned b, int offset = 0, unsigned f3 = 2) {
    Word v = Word(offset) & 0xfff;
    return (v >> 5) << 25 | b << 20 | a << 15 | f3 << 12 | (v & 31) << 7 | 0x23;
}

Word branch(unsigned a, unsigned b, int delta, unsigned f3 = 0) {
    Word v = Word(delta) & 0x1fff;
    return (v >> 12) << 31 | ((v >> 5) & 63) << 25 | b << 20 | a << 15 | f3 << 12 |
           ((v >> 1) & 15) << 8 | ((v >> 11) & 1) << 7 | 0x63;
}

constexpr Word stop = 0x00100073;

Machine make(
    Model model, const std::vector<Word>& words, bool forwarding = true, std::size_t size = 2048) {
    Machine m(model, size, forwarding);
    unsigned pc = 0x100;
    for (const auto w : words) {
        m.memory().write(pc, 4, w);
        pc += 4;
    }

    m.reset(0x100);

    return m;
}

void run(Machine& m) {
    unsigned ticks = 0;
    while (!m.halted() && ticks++ < 100000) {
        m.tick();
    }

    CHECK(m.halted());
}

struct Snapshot {
    std::array<Word, 32> registers;
    std::vector<std::uint8_t> memory;
    Address pc;
};

std::string records(Machine& m, std::vector<Snapshot>* snapshots = nullptr) {
    std::ostringstream out;
    m.on_retire = [&](const Retirement& e) {
        write_retirement(out, e, true);
        if (snapshots) {
            snapshots->push_back({m.registers().values(), m.memory().bytes(), m.pc()});
        }
    };
    m.on_trap = [&](const Trap& t) {
        write_trap(out, t, true);
    };

    run(m);
    m.on_retire = {};
    m.on_trap = {};

    return out.str();
}

void compare(const std::vector<Word>& words, const std::function<void(Machine&)>& setup = {}) {
    auto f = make(Model::Functional, words);
    if (setup) {
        setup(f);
    }

    std::vector<Snapshot> expected_states;
    const auto expected = records(f, &expected_states);

    for (bool forwarding : {true, false}) {
        auto p = make(Model::Pipeline, words, forwarding);
        if (setup) {
            setup(p);
        }

        std::vector<Snapshot> actual_states;
        CHECK(records(p, &actual_states) == expected);
        CHECK(actual_states.size() == expected_states.size());

        for (std::size_t i = 0; i < actual_states.size(); ++i) {
            CHECK(actual_states[i].registers == expected_states[i].registers);
            CHECK(actual_states[i].memory == expected_states[i].memory);
            CHECK(actual_states[i].pc == expected_states[i].pc);
        }

        CHECK(p.registers().values() == f.registers().values());
        CHECK(p.memory().bytes() == f.memory().bytes());
        CHECK(p.pc() == f.pc());
    }
}

void registers_memory() {
    RegisterFile r;
    for (unsigned i = 0; i < 32; ++i) {
        CHECK(r.read(i) == 0);
    }

    r.write(0, 123);
    CHECK(r.read(0) == 0);

    r.write(31, 0xffffffff);
    r.reset();
    CHECK(r.read(31) == 0);

    Memory m(7);
    m.write(0, 4, 0x89abcdef);
    CHECK(m.read(0, 1) == 0xef);
    CHECK(m.read(1, 1) == 0xcd);
    CHECK(m.read(2, 2) == 0x89ab);

    m.write(4, 2, 0x1234);
    const auto before = m.bytes();

    rejects([&] {
        m.write(4, 4, 0);
    });
    CHECK(m.bytes() == before);

    rejects([&] {
        m.write(1, 4, 0);
    });
    CHECK(m.bytes() == before);

    rejects([&] {
        m.write(6, 2, 0);
    });
    CHECK(m.bytes() == before);

    rejects([&] {
        m.read(0xfffffffc, 4);
    });

    rejects([&] {
        m.read(2, 4, true);
    });

    rejects([&] {
        m.read(0, 3);
    });

    rejects([&] {
        m.initialize(6, {1, 2});
    });
    CHECK(m.bytes() == before);

    rejects([] {
        Memory invalid(0);
    });
}

void decoding() {
    // Literal words specified independently of the encoder helpers above.
    const std::pair<Word, Op> examples[] = {{0x00500513, Op::ADDI}, {0xfff10093, Op::ADDI},
        {0x003100b3, Op::ADD}, {0x403100b3, Op::SUB}, {0x003110b3, Op::SLL}, {0x003120b3, Op::SLT},
        {0x003130b3, Op::SLTU}, {0x003140b3, Op::XOR}, {0x003150b3, Op::SRL}, {0x403150b3, Op::SRA},
        {0x003160b3, Op::OR}, {0x003170b3, Op::AND}, {0x00412083, Op::LW}, {0x00410083, Op::LB},
        {0x00411083, Op::LH}, {0x00414083, Op::LBU}, {0x00415083, Op::LHU}, {0x00310223, Op::SB},
        {0x00311223, Op::SH}, {0x00312223, Op::SW}, {0x00208463, Op::BEQ}, {0xfe209ee3, Op::BNE},
        {0x0020c463, Op::BLT}, {0x0020d463, Op::BGE}, {0x0020e463, Op::BLTU},
        {0x0020f463, Op::BGEU}, {0x008000ef, Op::JAL}, {0x00008067, Op::JALR},
        {0x123450b7, Op::LUI}, {0x12345097, Op::AUIPC}, {0x00311093, Op::SLLI},
        {0x00315093, Op::SRLI}, {0x40315093, Op::SRAI}, {0x00312093, Op::SLTI},
        {0x00313093, Op::SLTIU}, {0x00314093, Op::XORI}, {0x00316093, Op::ORI},
        {0x00317093, Op::ANDI}, {0x0ff0000f, Op::FENCE}, {0x00000073, Op::ECALL},
        {stop, Op::EBREAK}, {0xffffffff, Op::Illegal}, {0x023100b3, Op::Illegal},
        {0x02311093, Op::Illegal}, {0x0000100f, Op::Illegal}, {0x300110f3, Op::Illegal},
        {0x00200073, Op::Illegal}, {0x00002067, Op::Illegal}};

    for (const auto& e : examples) {
        CHECK(decode(e.first).op == e.second);
    }
    CHECK(decode(0xfff10093).imm == 0xffffffff);
    CHECK(decode(0xfe209ee3).imm == 0xfffffffc);
    CHECK(decode(0x008000ef).imm == 8);
    CHECK(decode(0x00312223).uses_rs2);
    CHECK(!decode(0x00500513).uses_rs2);
    CHECK(decode(0x00412003).load);
}

void arithmetic() {
    auto result = [](Word w, Word a, Word b = 0) {
        auto d = decode(w);
        return execute(d, 0x100, w, a, b).value;
    };

    CHECK(result(0x003100b3, 0xffffffff, 1) == 0);
    CHECK(result(0x403100b3, 0, 1) == 0xffffffff);
    CHECK(result(0xfff10093, 0) == 0xffffffff);

    CHECK(result(0x003120b3, 0x80000000, 0x7fffffff) == 1);
    CHECK(result(0x003130b3, 0x80000000, 0x7fffffff) == 0);

    CHECK(result(0x003110b3, 1, 33) == 2);
    CHECK(result(0x003150b3, 0x80000000, 32) == 0x80000000);
    CHECK(result(0x403150b3, 0x80000000, 31) == 0xffffffff);
    CHECK(result(0x403150b3, 0x80000000, 0) == 0x80000000);
    CHECK(result(0x40315093, 0x80000000) == 0xf0000000);
    CHECK(result(0x00315093, 0x80000000) == 0x10000000);
    CHECK(result(0x00311093, 0x20000001) == 8);

    CHECK(result(0xfff13093, 0xfffffffe) == 1);
    CHECK(result(0xfff12093, 0) == 0);

    CHECK(result(0x003140b3, 0xf0, 0x33) == 0xc3);
    CHECK(result(0x003160b3, 0xf0, 0x33) == 0xf3);
    CHECK(result(0x003170b3, 0xf0, 0x33) == 0x30);
    CHECK(result(0xfff14093, 0xf0) == 0xffffff0f);
    CHECK(result(0x00316093, 0xf0) == 0xf3);
    CHECK(result(0x00317093, 0xf0) == 0);

    CHECK(result(0x123450b7, 0) == 0x12345000);
    CHECK(result(0x12345097, 0) == 0x12345100);
}

void loads_stores() {
    std::vector<Word> w = {imm(1, 0, 512), imm(2, 0, -128), store(1, 2, 0, 0), store(1, 2, 2, 1),
        store(1, 2, 4, 2), imm(3, 1, 0, 0, 3), imm(4, 1, 0, 4, 3), imm(5, 1, 2, 1, 3),
        imm(6, 1, 2, 5, 3), imm(7, 1, 4, 2, 3), stop};
    compare(w);

    auto m = make(Model::Functional, w);

    run(m);
    CHECK(m.registers().read(3) == 0xffffff80);
    CHECK(m.registers().read(4) == 128);
    CHECK(m.registers().read(5) == 0xffffff80);
    CHECK(m.registers().read(6) == 65408);
    CHECK(m.registers().read(7) == 0xffffff80);
    CHECK(m.memory().read(512, 4) == 0xff800080);

    for (unsigned f3 : {0u, 1u, 2u, 4u, 5u}) {
        auto p = make(Model::Pipeline,
            {imm(1, 0, 2048), imm(0, 1, 0, f3, 3), stop}); // 2048 sign-extends: invalid address
        run(p);
        CHECK(p.terminal()->cause == Fault::LoadAccess);
        CHECK(p.statistics().retired == 1);
    }

    for (unsigned width : {1u, 2u, 4u}) {
        for (bool store_access : {false, true}) {
            const unsigned f3 = width == 1 ? 0 : width == 2 ? 1 : 2;
            auto p = make(Model::Pipeline,
                {store_access ? store(1, 2, 0, f3) : imm(3, 1, 0, f3, 3), stop}, true, 2051);
            p.registers().write(1, 2050);
            p.registers().write(2, 0xffffffff);
            const auto before = p.memory().bytes();
            run(p);
            if (width == 1) {
                CHECK(p.terminal()->cause == Fault::Ebreak);
            } else {
                CHECK(p.terminal()->cause ==
                      (width == 2 ? (store_access ? Fault::StoreAccess : Fault::LoadAccess)
                                  : (store_access ? Fault::StoreAlignment : Fault::LoadAlignment)));
                CHECK(p.memory().bytes() == before);
            }
        }
    }
}

void hazards_timing() {
    const std::vector<Word> chain = {imm(1, 0, 1), imm(2, 1, 2), reg(3, 2, 1), stop};
    compare(chain);

    auto on = make(Model::Pipeline, chain);
    run(on);
    CHECK(on.statistics().cycles == 8);
    CHECK(on.statistics().retired == 3);

    auto off = make(Model::Pipeline, chain, false);
    run(off);
    CHECK(off.statistics().cycles == 12);
    CHECK(off.statistics().raw_stalls == 4);

    auto empty = make(Model::Pipeline, {stop});
    run(empty);
    CHECK(empty.statistics().cycles == 5);
    CHECK(empty.statistics().retired == 0);

    const std::vector<Word> loaduse = {imm(1, 0, 512), imm(2, 1, 0, 2, 3), reg(3, 2, 2), stop};
    auto p = make(Model::Pipeline, loaduse);
    p.memory().write(512, 4, 21);
    run(p);
    CHECK(p.registers().read(3) == 42);
    CHECK(p.statistics().cycles == 9);
    CHECK(p.statistics().load_use_stalls == 1);
    compare(loaduse, [](Machine& m) {
        m.memory().write(512, 4, 21);
    });

    auto po = make(Model::Pipeline, loaduse, false);
    run(po);
    CHECK(po.statistics().cycles == 12);

    // Newest writer wins, x0 never produces, and store data/address have independent dependencies.
    compare({imm(1, 0, 1), imm(1, 1, 2), reg(2, 1, 1), imm(0, 1, 9), reg(3, 0, 2), imm(4, 0, 512),
        store(4, 3), imm(5, 4, 0, 2, 3), stop});
    compare({imm(1, 0, 512), imm(2, 1, 0, 2, 3), store(1, 2, 4), imm(3, 1, 4, 2, 3), stop},
        [](Machine& m) {
            m.memory().write(512, 4, 99);
        });
    compare({imm(1, 0, 512), imm(2, 1, 0, 2, 3), imm(3, 2, 0, 2, 3), stop}, [](Machine& m) {
        m.memory().write(512, 4, 516);
        m.memory().write(516, 4, 123);
    });
    compare({imm(1, 0, 512), imm(2, 0, 5), imm(2, 1, 0, 2, 3), reg(3, 2, 2), stop}, [](Machine& m) {
        m.memory().write(512, 4, 21);
    });
    compare({imm(0, 0, 1, 2, 3), stop}); // load to x0 must still fault on misalignment

    auto fwd = make(Model::Pipeline, {imm(1, 0, 2), imm(1, 1, 3), reg(2, 1, 1), stop});
    std::vector<std::string> choices;
    fwd.on_cycle = [&](const CycleEvent& e) {
        for (const auto& s : e.forwarding) {
            choices.push_back(s);
        }
    };
    run(fwd);
    CHECK(fwd.registers().read(2) == 10);
    CHECK(!choices.empty());
}

void control_precision() {
    // Known two-cycle redirect penalty, no data dependency stall with forwarding.
    auto p = make(
        Model::Pipeline, {imm(1, 0, 1), branch(1, 1, 12), store(0, 1), stop, imm(10, 0, 7), stop});
    run(p);
    CHECK(p.statistics().cycles == 10);
    CHECK(p.statistics().redirects == 1);
    CHECK(p.statistics().flushed == 1);
    CHECK(p.registers().read(10) == 7);
    CHECK(p.memory().read(0, 4) == 0);
    compare({imm(1, 0, 1), branch(1, 1, 12), store(0, 1), stop, imm(10, 0, 7), stop});
    compare({branch(0, 0, 8), stop, imm(10, 0, 42), stop});
    compare({branch(0, 0, 8), 0xffffffff, imm(10, 0, 42), stop});
    compare({branch(0, 0, 8), 0x00000073, imm(10, 0, 42), stop});
    compare({branch(0, 0, 8), imm(1, 0, 1, 2, 3), imm(10, 0, 42), stop});

    auto branch_dep = make(Model::Pipeline, {imm(1, 0, 1), branch(1, 0, 8), imm(10, 0, 42), stop});
    run(branch_dep);
    CHECK(branch_dep.registers().read(10) == 42);
    compare({imm(1, 0, 1), branch(1, 0, 8), imm(10, 0, 42), stop});

    // Taken/not-taken target alignment: fault belongs to branch, only if taken.

    auto bad = make(Model::Pipeline, {branch(0, 0, 2), imm(1, 0, 9), stop});
    run(bad);
    CHECK(bad.terminal()->cause == Fault::InstructionAlignment);
    CHECK(bad.pc() == 0x100);
    CHECK(bad.registers().read(1) == 0);

    auto nt = make(Model::Pipeline, {branch(0, 0, 2, 1), stop});
    run(nt);
    CHECK(nt.terminal()->cause == Fault::Ebreak);

    for (unsigned f3 : {0u, 1u, 4u, 5u, 6u, 7u}) {
        compare({imm(1, 0, -1), imm(2, 0, 1), branch(1, 2, 8, f3), imm(10, 0, 5), stop});
    }
    compare({imm(1, 0, 4), imm(1, 1, -1), branch(1, 0, -4, 1), stop});

    // JAL link and immediate link consumer, then JALR base + clear bit 0.
    compare({0x008000ef, stop, imm(2, 1, 1), imm(3, 0, 0x115), imm(4, 3, 0, 0, 0x67), imm(5, 4, 1),
        stop});

    auto j = make(Model::Pipeline, {0x002000ef, stop});
    run(j);
    CHECK(j.terminal()->cause == Fault::InstructionAlignment);
    CHECK(j.registers().read(1) == 0);

    // Older store -> immediately following load; both commit in order.
    auto s = make(Model::Pipeline, {imm(1, 0, 512), imm(2, 0, 77), store(1, 2), imm(3, 1, 0, 2, 3),
                                       imm(4, 0, 1, 2, 3), imm(2, 0, 99), store(1, 2, 4), stop});
    run(s);
    CHECK(s.registers().read(3) == 77);
    CHECK(s.registers().read(2) == 77);
    CHECK(s.memory().read(512, 4) == 77);
    CHECK(s.memory().read(516, 4) == 0);
    CHECK(s.pc() == 0x110);
    CHECK(s.terminal()->cause == Fault::LoadAlignment);
    CHECK(s.statistics().retired == 4);
    compare({imm(1, 0, 512), imm(2, 0, 77), store(1, 2), imm(3, 1, 0, 2, 3), imm(4, 0, 1, 2, 3),
        imm(2, 0, 99), store(1, 2, 4), stop});

    // MEM fault outranks a younger EX jump and its link write.
    compare({imm(1, 0, 1, 2, 3), 0x008000ef, stop, imm(2, 0, 7), stop});

    auto store_fault =
        make(Model::Pipeline, {imm(1, 0, 1), imm(2, 0, 77), store(1, 2), imm(2, 0, 99), stop});
    const auto pristine = store_fault.memory().bytes();
    run(store_fault);
    CHECK(store_fault.terminal()->cause == Fault::StoreAlignment);
    CHECK(store_fault.registers().read(2) == 77);
    CHECK(store_fault.memory().bytes() == pristine);
    CHECK(store_fault.statistics().retired == 2);

    // Older redirect discards a speculative fetch fault at end of RAM.
    Machine end(Model::Pipeline, 512);
    end.memory().write(508, 4, 0x00400067);
    end.reset(508); // jalr x0,4(x0)
    end.memory().write(4, 4, stop);
    run(end);
    CHECK(end.pc() == 4);
    CHECK(end.terminal()->cause == Fault::Ebreak);

    auto ec = make(Model::Pipeline, {imm(1, 0, 7), 0x00000073, imm(1, 0, 9), stop});
    run(ec);
    CHECK(ec.terminal()->cause == Fault::Ecall);
    CHECK(ec.registers().read(1) == 7);
    CHECK(ec.pc() == 0x104);

    auto ill = make(Model::Pipeline, {imm(1, 0, 7), 0xffffffff, store(0, 1), stop});
    run(ill);
    CHECK(ill.terminal()->cause == Fault::IllegalInstruction);
    CHECK(ill.registers().read(1) == 7);
    CHECK(ill.memory().read(0, 4) == 0);

    // Reset preserves loaded memory while clearing architecture and timing.

    ill.reset(0x100);
    CHECK(!ill.halted());
    CHECK(ill.statistics().cycles == 0);
    CHECK(ill.registers().read(1) == 0);
    CHECK(ill.memory().read(0x100, 4) == imm(1, 0, 7));

    for (Address entry : {2u, 2048u}) {
        auto fetch_fault = make(Model::Pipeline, {stop});
        fetch_fault.reset(entry);
        run(fetch_fault);
        CHECK(fetch_fault.pc() == entry);
        CHECK(fetch_fault.statistics().retired == 0);
        CHECK(fetch_fault.statistics().cycles == 5);
        CHECK(fetch_fault.terminal()->cause ==
              (entry == 2 ? Fault::InstructionAlignment : Fault::InstructionAccess));
        compare({stop}, [entry](Machine& m) {
            m.reset(entry);
        });
    }
}

std::vector<std::uint8_t> elf_fixture() {
    std::vector<std::uint8_t> b(88, 0);
    auto put = [&](unsigned off, Word v, unsigned n = 4) {
        for (unsigned i = 0; i < n; ++i) {
            b[off + i] = std::uint8_t(v >> (8 * i));
        }
    };
    put(0, 0x464c457f);
    put(4, 0x00010101);
    put(16, 2, 2);
    put(18, 243, 2);
    put(20, 1);
    put(24, 256);
    put(28, 52);
    put(40, 52, 2);
    put(42, 32, 2);
    put(44, 1, 2);
    put(52, 1);
    put(56, 84);
    put(60, 256);
    put(64, 256);
    put(68, 4);
    put(72, 12);
    put(76, 5);
    put(80, 4);
    put(84, stop);
    return b;
}

void loaders() {
    auto b = elf_fixture();
    Memory mem(512);
    mem.write(260, 4, 0xffffffff);
    CHECK(load_elf(mem, b).entry == 256);
    CHECK(mem.read(256, 4) == stop);
    CHECK(mem.read(260, 4) == 0);
    CHECK(mem.read(264, 4) == 0);

    for (unsigned off : {0u, 4u, 5u, 6u, 7u, 16u, 18u, 20u, 36u, 40u, 42u, 44u, 52u, 56u, 60u, 64u,
             68u, 73u, 80u}) {
        auto bad = b;
        bad[off] = 0xff;
        Memory m(512);
        const auto before = m.bytes();

        rejects([&] {
            load_elf(m, bad);
        });
        CHECK(m.bytes() == before);
    }

    for (unsigned size : {0u, 1u, 51u, 70u, 87u}) {
        auto bad = b;
        bad.resize(size);

        rejects([&] {
            load_elf(mem, bad);
        });
    }

    auto bad = b;
    bad[76] = 4;

    rejects([&] {
        load_elf(mem, bad);
    });

    rejects([&] {
        load_raw(mem, {}, 0, 0);
    });

    rejects([&] {
        load_raw(mem, {1, 2, 3, 4}, 510, 510);
    });

    rejects([&] {
        load_raw(mem, {1, 2, 3, 4}, 0, 4);
    });
    CHECK(load_raw(mem, {0x73, 0, 0x10, 0}, 0, 0).entry == 0);
}

void random_programs() {
    for (unsigned seed : {1u, 42u, 2026u, 0xc0ffeeu}) {
        std::mt19937 rng(seed);
        std::vector<Word> w;
        std::array<Word, 32> expected{};
        for (unsigned i = 0; i < 400; ++i) {
            const unsigned rd = unsigned(rng() % 32), a = unsigned(rng() % 32),
                           b = unsigned(rng() % 32), kind = unsigned(rng() % 6);
            Word value = 0;
            if (kind == 0) {
                const int k = int(rng() % 4096) - 2048;
                w.push_back(imm(rd, a, k));
                value = expected[a] + Word(k);
            } else {
                const unsigned f3 = kind == 1   ? 0
                                    : kind == 2 ? 4
                                    : kind == 3 ? 6
                                    : kind == 4 ? 7
                                                : 1;
                w.push_back(reg(rd, a, b, f3));
                if (kind == 1) {
                    value = expected[a] + expected[b];
                }
                if (kind == 2) {
                    value = expected[a] ^ expected[b];
                }
                if (kind == 3) {
                    value = expected[a] | expected[b];
                }
                if (kind == 4) {
                    value = expected[a] & expected[b];
                }
                if (kind == 5) {
                    value = expected[a] << (expected[b] % 32);
                }
            }
            if (rd) {
                expected[rd] = value;
            }
        }
        w.push_back(stop);

        auto m = make(Model::Functional, w);

        run(m);
        CHECK(m.registers().values() == expected);
        compare(w);

        // Bounded generated backward control flow plus memory, independently expected loop sum.
        const int n = 1 + int(rng() % 30);

        std::vector<Word> loop = {imm(1, 0, n), imm(2, 0, 0), reg(2, 2, 1), imm(1, 1, -1),
            branch(1, 0, -8, 1), imm(3, 0, 512), store(3, 2), stop};
        auto l = make(Model::Functional, loop);
        run(l);
        CHECK(l.registers().read(2) == Word(n * (n + 1) / 2));
        compare(loop);
    }
}

int main() {
    try {
        std::cout << "registers/memory\n";
        registers_memory();
        std::cout << "decode\n";
        decoding();
        std::cout << "arithmetic\n";
        arithmetic();
        std::cout << "loads/stores\n";
        loads_stores();
        std::cout << "hazards/timing\n";
        hazards_timing();
        std::cout << "control/precision\n";
        control_precision();
        std::cout << "loaders\n";
        loaders();
        std::cout << "random/differential\n";
        random_programs();
        std::cout << "8 core test groups passed (including deterministic differential and timing "
                     "cases)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
