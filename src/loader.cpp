#include "rv/loader.hpp"
#include <algorithm>
#include <fstream>
#include <iterator>

namespace rv {
    std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            throw std::invalid_argument("cannot open program: " + path.string());
        }

        std::vector<std::uint8_t> result((std::istreambuf_iterator<char>(f)), {});
        if (f.bad()) {
            throw std::invalid_argument("cannot read program");
        }

        return result;
    }

    Image load_raw(
        Memory& memory, const std::vector<std::uint8_t>& bytes, Address load, Address entry) {
        if (bytes.empty()) {
            throw std::invalid_argument("empty raw image");
        }

        if ((entry & 3) || entry < load ||
            std::uint64_t(entry) + 4 > std::uint64_t(load) + bytes.size()) {
            throw std::invalid_argument(
                "raw entry must address a complete aligned instruction in image");
        }

        memory.initialize(load, bytes);

        return {entry, "bin"};
    }

    Image load_elf(Memory& memory, const std::vector<std::uint8_t>& b) {
        auto fail = [] {
            throw std::invalid_argument("unsupported or malformed ELF32 RISC-V executable");
        };

        auto get = [&](std::size_t off, unsigned n) -> Word {
            // Read a bounded little-endian ELF field without casting guest bytes to a
            // host struct (which could have different padding, alignment, or endianness).
            if (off > b.size() || n > b.size() - off) {
                fail();
            }

            Word v = 0;
            for (unsigned i = 0; i < n; ++i) {
                v |= Word(b[off + i]) << (i * 8);
            }

            return v;
        };

        // ELF starts with bytes 7f 'E' 'L' 'F'. Require 32-bit, little-endian, static
        // RISC-V executable headers; reject incompatible ABI flags before changing RAM.
        if (b.size() < 52 || get(0, 4) != 0x464c457f || get(4, 1) != 1 || get(5, 1) != 1 ||
            get(6, 1) != 1 || get(7, 1) != 0 || get(16, 2) != 2 || get(18, 2) != 243 ||
            get(20, 4) != 1 || get(36, 4) != 0 || get(40, 2) != 52 || get(42, 2) != 32) {
            fail();
        }

        const auto entry = get(24, 4), phoff = get(28, 4), count = get(44, 2);
        // Program headers describe what to load. Section/symbol tables are unnecessary
        // for execution; the entry field supplies the first instruction PC.
        if (!count || (entry & 3) || std::uint64_t(phoff) + std::uint64_t(count) * 32 > b.size()) {
            fail();
        }

        struct Segment {
            Word address, size, offset, filesz, flags;
        };

        std::vector<Segment> segments;
        bool entry_valid = false;

        for (unsigned i = 0; i < count; ++i) {
            const auto off = std::size_t(phoff) + i * 32;
            const auto type = get(off, 4);

            // Reject dynamic/interpreter/TLS requirements. Harmless NOTE/PHDR/GNU_STACK are
            // ignored.
            if (type == 2 || type == 3 || type == 7) {
                fail();
            }
            if (type != 1) {
                continue;
            }

            const auto fileoff = get(off + 4, 4), address = get(off + 8, 4),
                       physical = get(off + 12, 4);
            const auto filesz = get(off + 16, 4), memsz = get(off + 20, 4),
                       flags = get(off + 24, 4), align = get(off + 28, 4);

            if (physical != address || filesz > memsz ||
                std::uint64_t(fileoff) + filesz > b.size() ||
                std::uint64_t(address) + memsz > memory.size() ||
                (align > 1 && ((align & (align - 1)) || address % align != fileoff % align))) {
                fail();
            }
            if (!memsz) {
                continue;
            }

            for (const auto& s : segments) {
                if (std::uint64_t(address) < std::uint64_t(s.address) + s.size &&
                    std::uint64_t(s.address) < std::uint64_t(address) + memsz) {
                    fail();
                }
            }

            segments.push_back({address, memsz, fileoff, filesz, flags});
            if ((flags & 1) && entry >= address &&
                std::uint64_t(entry) + 4 <= std::uint64_t(address) + filesz) {
                entry_valid = true;
            }
        }

        if (!entry_valid) {
            fail();
        }

        // All validation precedes all mutation, including zero-fill of BSS.
        // filesz bytes come from the ELF; the remaining memsz-filesz bytes start at zero.
        for (const auto& s : segments) {
            std::vector<std::uint8_t> data(s.size, 0);
            std::copy_n(b.begin() + s.offset, s.filesz, data.begin());
            memory.initialize(s.address, data);
        }

        return {entry, "elf"};
    }
} // namespace rv
