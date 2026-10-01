// registers.h - Task 2 (registers) and Task 6 (32-bit views of the same registers)
#pragma once

#include <array>
#include <cstdint>
#include <string>

// A decoded register operand: which register, and whether it is the 32-bit (Wn) view.
struct RegRef {
    enum Kind { GPR, SP, ZR, PC };
    Kind kind;
    int index;   // 0-30 for GPR, unused otherwise
    bool is32;   // true for Wn / WZR / WSP
};

class Registers {
public:
    std::array<std::uint64_t, 31> x{};  // X0-X30
    std::uint64_t sp = 0;
    std::uint64_t pc = 0;

    // Processor state. N and Z are printed. C and V are tracked internally only,
    // because B.GT / B.LE need V to be correct when a subtraction overflows.
    bool n = false;
    bool z = false;
    bool c = false;
    bool v = false;

    // Decodes "X5", "w3", "SP", "WSP", "XZR", "WZR", "PC". Returns false if invalid.
    static bool parse(const std::string &name, RegRef &out);

    // 32-bit reads return only the low 32 bits.
    // 32-bit writes store the low 32 bits and zero the upper 32 bits.
    std::uint64_t read(const RegRef &r) const;
    void write(const RegRef &r, std::uint64_t value);

    // Convenience wrappers by name (throw std::invalid_argument on a bad name).
    std::uint64_t read(const std::string &name) const;
    void write(const std::string &name, std::uint64_t value);

    // Sets N, Z, C, V as a - b would (CMP), using 32- or 64-bit width.
    void setFlagsSub(std::uint64_t a, std::uint64_t b, bool is32);

    void print() const;
};
