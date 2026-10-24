#pragma once

#include "rv/memory.hpp"
#include <filesystem>

namespace rv {
    struct Image {
        Address entry = 0; // Where reset starts fetching; not necessarily RAM address zero.
        std::string format;
    };

    // Raw bytes carry no addresses, so the caller supplies load and entry addresses.
    Image load_raw(
        Memory& memory, const std::vector<std::uint8_t>& bytes, Address load, Address entry);
    // ELF program headers supply segment addresses, entry point, and BSS zero-fill sizes.
    Image load_elf(Memory& memory, const std::vector<std::uint8_t>& bytes);
    std::vector<std::uint8_t> read_file(const std::filesystem::path& path);
} // namespace rv
