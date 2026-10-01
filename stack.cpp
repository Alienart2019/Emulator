#include "stack.h"

#include <cstdio>
#include <stdexcept>

#include "util.h"

using namespace std;

Stack::Stack(uint64_t baseAddr) : base_(baseAddr) { mem_.fill(0); }

bool Stack::inRange(uint64_t addr, size_t len) const {
    return len <= SIZE && addr >= base_ && (addr - base_) <= SIZE - len;
}

uint64_t Stack::read(uint64_t addr, size_t len) const {
    if (!inRange(addr, len)) throw out_of_range("stack read out of range");
    uint64_t value = 0;
    for (size_t i = 0; i < len; i++) value |= (uint64_t)mem_[addr - base_ + i] << (8 * i);
    return value;
}

void Stack::write(uint64_t addr, uint64_t value, size_t len) {
    if (!inRange(addr, len)) throw out_of_range("stack write out of range");
    for (size_t i = 0; i < len; i++) mem_[addr - base_ + i] = (uint8_t)(value >> (8 * i));
}

void Stack::print() const {
    printBanner("Stack:");
    for (size_t off = 0; off < SIZE; off += 16) {
        printf("%08llx ", (unsigned long long)(base_ + off));
        for (size_t i = 0; i < 16; i++) printf("%02x ", mem_[off + i]);
        printf("|");
        for (size_t i = 0; i < 16; i++) {
            uint8_t c = mem_[off + i];
            printf("%c", (c >= 0x20 && c <= 0x7e) ? c : '.');
        }
        printf("|\n");
    }
    printf("%08llx\n", (unsigned long long)(base_ + SIZE));
}
