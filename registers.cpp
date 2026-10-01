#include "registers.h"

#include <cctype>
#include <cstdio>
#include <stdexcept>

#include "util.h"

using namespace std;

bool Registers::parse(const string &name, RegRef &out) {
    string u = upper(trim(name));
    if (u == "XZR") { out = RegRef{RegRef::ZR, 0, false}; return true; }
    if (u == "WZR") { out = RegRef{RegRef::ZR, 0, true};  return true; }
    if (u == "SP")  { out = RegRef{RegRef::SP, 0, false}; return true; }
    if (u == "WSP") { out = RegRef{RegRef::SP, 0, true};  return true; }
    if (u == "PC")  { out = RegRef{RegRef::PC, 0, false}; return true; }

    if (u.size() < 2 || u.size() > 3 || (u[0] != 'X' && u[0] != 'W')) return false;
    for (size_t i = 1; i < u.size(); i++)
        if (!isdigit((unsigned char)u[i])) return false;
    int idx = stoi(u.substr(1));
    if (idx > 30) return false;
    out = RegRef{RegRef::GPR, idx, u[0] == 'W'};
    return true;
}

uint64_t Registers::read(const RegRef &r) const {
    uint64_t value = 0;
    switch (r.kind) {
        case RegRef::GPR: value = x[r.index]; break;
        case RegRef::SP:  value = sp;         break;
        case RegRef::PC:  value = pc;         break;
        case RegRef::ZR:  value = 0;          break;
    }
    return r.is32 ? (value & 0xFFFFFFFFULL) : value;
}

void Registers::write(const RegRef &r, uint64_t value) {
    if (r.is32) value &= 0xFFFFFFFFULL;  // upper 32 bits end up zero
    switch (r.kind) {
        case RegRef::GPR: x[r.index] = value; break;
        case RegRef::SP:  sp = value;         break;
        case RegRef::PC:  pc = value;         break;
        case RegRef::ZR:  break;              // writes to the zero register are discarded
    }
}

uint64_t Registers::read(const string &name) const {
    RegRef r;
    if (!parse(name, r)) throw invalid_argument("invalid register: " + name);
    return read(r);
}

void Registers::write(const string &name, uint64_t value) {
    RegRef r;
    if (!parse(name, r)) throw invalid_argument("invalid register: " + name);
    write(r, value);
}

void Registers::setFlagsSub(uint64_t a, uint64_t b, bool is32) {
    const uint64_t mask = is32 ? 0xFFFFFFFFULL : ~0ULL;
    const uint64_t signBit = is32 ? (1ULL << 31) : (1ULL << 63);
    a &= mask;
    b &= mask;
    uint64_t r = (a - b) & mask;

    n = (r & signBit) != 0;
    z = (r == 0);
    c = (a >= b);                                // no borrow
    v = (((a ^ b) & (a ^ r)) & signBit) != 0;    // signed overflow
}

void Registers::print() const {
    printBanner("Registers:");
    auto cell = [](const string &name, uint64_t value) {
        printf("%-5s0x%016llx   ", (name + ":").c_str(), (unsigned long long)value);
    };
    // 3 columns: X0-X9 | X10-X19 | X20-X29, then a final row of SP, PC, X30.
    for (int i = 0; i < 10; i++) {
        cell("X" + to_string(i), x[i]);
        cell("X" + to_string(i + 10), x[i + 10]);
        cell("X" + to_string(i + 20), x[i + 20]);
        printf("\n");
    }
    cell("SP", sp);
    cell("PC", pc);
    cell("X30", x[30]);
    printf("\n");
    printf("Processor State N bit: %d\n", n ? 1 : 0);
    printf("Processor State Z bit: %d\n", z ? 1 : 0);
}
