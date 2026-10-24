// Independent adapter for Spike v1.1.0, commit 530af85d83781a3dae31a4ace84a573ec255fefa.
// Does not include or link any rvcore code. All RAM accesses go through checked MMIO,
// avoiding Spike's default boot ROM, CLINT, proxy kernel, and memory map.
#include "processor.h"
#include "simif.h"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <tuple>
#include <vector>

#ifndef RISCV_ENABLE_COMMITLOG
#error Build Spike with --enable-commitlog
#endif

class FlatRAM : public simif_t {
public:
    std::vector<std::uint8_t> bytes;

    explicit FlatRAM(std::size_t size) : bytes(size, 0) {}

    char* addr_to_mem(reg_t) override {
        return nullptr;
    }

    bool mmio_load(reg_t a, std::size_t n, std::uint8_t* out) override {
        if (a > bytes.size() || n > bytes.size() - a) {
            return false;
        }

        for (std::size_t i = 0; i < n; ++i) {
            out[i] = bytes[a + i];
        }

        return true;
    }

    bool mmio_store(reg_t a, std::size_t n, const std::uint8_t* in) override {
        if (a > bytes.size() || n > bytes.size() - a) {
            return false;
        }

        for (std::size_t i = 0; i < n; ++i) {
            bytes[a + i] = in[i];
        }

        return true;
    }

    void proc_reset(unsigned) override {}

    const char* get_symbol(std::uint64_t) override {
        return nullptr;
    }

    std::uint32_t instruction(std::uint32_t pc) const {
        if ((pc & 3) || std::uint64_t(pc) + 4 > bytes.size()) {
            throw std::runtime_error("outside successful aligned overlap environment");
        }

        return std::uint32_t(bytes[pc]) | std::uint32_t(bytes[pc + 1]) << 8 |
               std::uint32_t(bytes[pc + 2]) << 16 | std::uint32_t(bytes[pc + 3]) << 24;
    }
};

int main(int argc, char** argv) {
    try {
        if (argc != 6) {
            throw std::runtime_error("spike_adapter RAW LOAD ENTRY RAM_SIZE MAX_INSTRUCTIONS");
        }

        const auto load = std::stoull(argv[2], nullptr, 0),
                   entry = std::stoull(argv[3], nullptr, 0),
                   size = std::stoull(argv[4], nullptr, 0),
                   limit = std::stoull(argv[5], nullptr, 0);
        if (!size || size > 0x100000000ull || entry > 0xffffffffull) {
            throw std::runtime_error("invalid map");
        }

        FlatRAM ram(static_cast<std::size_t>(size));
        std::ifstream f(argv[1], std::ios::binary);
        if (!f) {
            throw std::runtime_error("cannot open raw image");
        }

        std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(f)), {});
        if (load > size || raw.size() > size - load) {
            throw std::runtime_error("image outside map");
        }

        for (std::size_t i = 0; i < raw.size(); ++i) {
            ram.bytes[load + i] = raw[i];
        }

        FILE* log = std::tmpfile();
        if (!log) {
            throw std::runtime_error("cannot open Spike log sink");
        }

        {
            processor_t cpu("RV32I", "M", "vlen:128,elen:64", &ram, 0, false, log, std::cerr);
            cpu.set_pmp_num(0); // Bare physical M-mode, no PMP constraints or virtual memory.
            cpu.enable_log_commits();
            auto* state = cpu.get_state();
            state->pc = entry;
            for (unsigned i = 0; i < 32; ++i) {
                state->XPR.write(i, 0);
            }
            std::uint64_t sequence = 0;
            while (true) {
                const auto pc = std::uint32_t(state->pc), w = ram.instruction(pc);
                // Our agreed environment defines EBREAK completion BEFORE privileged trap handling.
                // No normal instruction is bypassed. Other traps are explicitly outside this
                // harness.
                if (w == 0x00100073) {
                    std::cout << "{\"type\":\"trap\",\"cause\":\"ebreak\",\"pc\":" << pc
                              << ",\"value\":0}\n";
                    std::cout << "{\"type\":\"state\",\"pc\":" << pc << ",\"registers\":[";
                    for (unsigned i = 0; i < 32; ++i) {
                        if (i) {
                            std::cout << ',';
                        }
                        std::cout << std::uint32_t(state->XPR[i]);
                    }
                    std::cout << "],\"memory_nonzero_bytes\":{";
                    bool first = true;
                    for (std::size_t i = 0; i < ram.bytes.size(); ++i) {
                        if (ram.bytes[i]) {
                            if (!first) {
                                std::cout << ',';
                            }
                            first = false;
                            std::cout << '"' << i << "\":" << unsigned(ram.bytes[i]);
                        }
                    }
                    std::cout << "}}\n";
                    break;
                }
                if (sequence >= limit) {
                    throw std::runtime_error("adapter execution limit");
                }
                // A sentinel mtvec tells us whether step took a privileged trap, including self
                // jumps.
                cpu.set_csr(0x305, 0xfffffffc);
                cpu.step(1);
                if (std::uint32_t(state->pc) == 0xfffffffc) {
                    throw std::runtime_error(
                        "Spike trap outside successful-program overlap (mcause=" +
                        std::to_string(cpu.get_csr(0x342)) + ")");
                }
                std::cout << "{\"type\":\"retire\",\"sequence\":" << ++sequence << ",\"pc\":" << pc
                          << ",\"word\":" << w << ",\"register_write\":";
                unsigned rd = 0;
                for (const auto& item : state->log_reg_write) {
                    if ((item.first & 15) == 0 && item.first != 0) {
                        if (rd) {
                            throw std::runtime_error("multiple integer writes");
                        }
                        rd = unsigned(item.first >> 4);
                    }
                }
                if (rd) {
                    std::cout << "{\"register\":" << rd
                              << ",\"value\":" << std::uint32_t(state->XPR[rd]) << '}';
                } else {
                    std::cout << "null";
                }
                std::cout << ",\"memory_write\":";
                if (state->log_mem_write.size() > 1) {
                    throw std::runtime_error("multiple stores");
                }
                if (!state->log_mem_write.empty()) {
                    const auto& m = state->log_mem_write.front();
                    std::cout << "{\"address\":" << std::get<0>(m)
                              << ",\"value\":" << std::get<1>(m)
                              << ",\"width\":" << unsigned(std::get<2>(m)) << '}';
                } else {
                    std::cout << "null";
                }
                std::cout << ",\"next_pc\":" << std::uint32_t(state->pc) << "}\n";
            }
        }
        std::fclose(log);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
