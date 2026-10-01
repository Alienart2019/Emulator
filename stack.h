// stack.h - Task 3: 256 bytes of stack memory
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

class Stack {
public:
    static constexpr std::size_t SIZE = 256;

    explicit Stack(std::uint64_t baseAddr = 0x0);

    std::uint64_t base() const { return base_; }

    // True if [addr, addr + len) lies entirely inside the stack.
    bool inRange(std::uint64_t addr, std::size_t len) const;

    // Little-endian read/write of `len` bytes (1..8). Throw std::out_of_range if outside the stack.
    std::uint64_t read(std::uint64_t addr, std::size_t len) const;
    void write(std::uint64_t addr, std::uint64_t value, std::size_t len);

    // hexdump -C style: offset, 16 hex bytes, ASCII gutter.
    void print() const;

private:
    std::uint64_t base_;
    std::array<std::uint8_t, SIZE> mem_;
};
