// ARM64 Emulator - Tasks 1-3 (parser, registers, stack)
// Build: g++ -std=c++17 -Wall -Wextra -o emulator emulator.cpp
// Usage: ./emulator <input.asm>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Output helpers
// ---------------------------------------------------------------------------
static const int LINE_WIDTH = 127;

static void printBanner(const std::string &title) {
    std::string bar(LINE_WIDTH, '-');
    std::printf("%s\n%s\n%s\n", bar.c_str(), title.c_str(), bar.c_str());
}

static std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace((unsigned char)s[b])) b++;
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

// ---------------------------------------------------------------------------
// Task 2: Registers
// ---------------------------------------------------------------------------
// X0-X30 live in x[]. XZR always reads 0 and ignores writes.
// SP and PC are separate. Processor state only tracks N and Z.
struct Registers {
    std::array<uint64_t, 31> x{};
    uint64_t sp = 0;
    uint64_t pc = 0;
    bool n = false;
    bool z = false;

    // Returns true if `name` is a valid 64-bit register name (X0-X30, XZR, SP, PC).
    static bool isRegister(const std::string &name) {
        std::string u = upper(name);
        if (u == "XZR" || u == "SP" || u == "PC") return true;
        if (u.size() < 2 || u[0] != 'X') return false;
        for (size_t i = 1; i < u.size(); i++)
            if (!std::isdigit((unsigned char)u[i])) return false;
        if (u.size() > 3) return false;
        return std::stoi(u.substr(1)) <= 30;
    }

    uint64_t read(const std::string &name) const {
        std::string u = upper(name);
        if (!isRegister(u)) throw std::invalid_argument("invalid register: " + name);
        if (u == "XZR") return 0;
        if (u == "SP") return sp;
        if (u == "PC") return pc;
        return x[std::stoi(u.substr(1))];
    }

    void write(const std::string &name, uint64_t value) {
        std::string u = upper(name);
        if (!isRegister(u)) throw std::invalid_argument("invalid register: " + name);
        if (u == "XZR") return;  // writes to XZR are discarded
        if (u == "SP") { sp = value; return; }
        if (u == "PC") { pc = value; return; }
        x[std::stoi(u.substr(1))] = value;
    }

    void print() const {
        printBanner("Registers:");
        // 3 columns: X0-X9 | X10-X19 | X20-X29, then a final row of SP, PC, X30.
        auto cell = [](const std::string &name, uint64_t v) {
            std::printf("%-5s0x%016llx   ", (name + ":").c_str(), (unsigned long long)v);
        };
        for (int i = 0; i < 10; i++) {
            cell("X" + std::to_string(i), x[i]);
            cell("X" + std::to_string(i + 10), x[i + 10]);
            cell("X" + std::to_string(i + 20), x[i + 20]);
            std::printf("\n");
        }
        cell("SP", sp);
        cell("PC", pc);
        cell("X30", x[30]);
        std::printf("\n");
        std::printf("Processor State N bit: %d\n", n ? 1 : 0);
        std::printf("Processor State Z bit: %d\n", z ? 1 : 0);
    }
};

// ---------------------------------------------------------------------------
// Task 3: Stack memory
// ---------------------------------------------------------------------------
class Stack {
public:
    static constexpr size_t SIZE = 256;

    explicit Stack(uint64_t baseAddr = 0x0) : base_(baseAddr) { mem_.fill(0); }

    uint64_t base() const { return base_; }

    bool inRange(uint64_t addr, size_t len) const {
        return addr >= base_ && addr - base_ + len <= SIZE;
    }

    // Little-endian read/write of `len` bytes (1..8). Used by later tasks (LDR/STR).
    uint64_t read(uint64_t addr, size_t len) const {
        if (!inRange(addr, len)) throw std::out_of_range("stack read out of range");
        uint64_t v = 0;
        for (size_t i = 0; i < len; i++) v |= (uint64_t)mem_[addr - base_ + i] << (8 * i);
        return v;
    }

    void write(uint64_t addr, uint64_t value, size_t len) {
        if (!inRange(addr, len)) throw std::out_of_range("stack write out of range");
        for (size_t i = 0; i < len; i++) mem_[addr - base_ + i] = (uint8_t)(value >> (8 * i));
    }

    // hexdump -C style: offset, 16 hex bytes, ASCII gutter.
    void print() const {
        printBanner("Stack:");
        for (size_t off = 0; off < SIZE; off += 16) {
            std::printf("%08llx ", (unsigned long long)(base_ + off));
            for (size_t i = 0; i < 16; i++) std::printf("%02x ", mem_[off + i]);
            std::printf("|");
            for (size_t i = 0; i < 16; i++) {
                uint8_t c = mem_[off + i];
                std::printf("%c", (c >= 0x20 && c <= 0x7e) ? c : '.');
            }
            std::printf("|\n");
        }
        std::printf("%08llx\n", (unsigned long long)(base_ + SIZE));
    }

private:
    uint64_t base_;
    std::array<uint8_t, SIZE> mem_;
};

// ---------------------------------------------------------------------------
// Task 1: Parser
// ---------------------------------------------------------------------------
struct Instruction {
    int lineNumber = 0;
    std::string mnemonic;
    std::vector<std::string> operands;
};

// Splits "X0, [SP, 0x08]" into {"X0", "[SP, 0x08]"}: commas inside [] are ignored.
static std::vector<std::string> splitOperands(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '[') depth++;
        if (c == ']') depth--;
        if (c == ',' && depth == 0) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!trim(cur).empty()) out.push_back(trim(cur));
    return out;
}

// Describes a memory operand: "[SP, 0x08]" -> "SP + 0x08". Returns "" if not a memory operand.
static std::string describeMemoryOperand(const std::string &op) {
    if (op.empty() || op[0] != '[') return "";
    size_t close = op.find(']');
    if (close == std::string::npos) throw std::invalid_argument("unterminated '[' in operand: " + op);

    std::vector<std::string> parts = splitOperands(op.substr(1, close - 1));
    if (parts.empty()) throw std::invalid_argument("empty memory operand");

    std::string desc = parts[0];
    if (parts.size() >= 2) {
        std::string off = parts[1];
        if (!off.empty() && off[0] == '#') off = off.substr(1);
        if (!off.empty() && off[0] == '-') desc += " - " + trim(off.substr(1));
        else desc += " + " + off;
    }
    for (size_t i = 2; i < parts.size(); i++) desc += " (" + parts[i] + ")";  // e.g. LSL 3

    std::string suffix = trim(op.substr(close + 1));
    if (suffix == "!") desc += " (pre-index, writeback)";
    return desc;
}

// Each supported instruction has its own parser. For now they validate the operand
// count and the memory-operand syntax; later tasks can add an execute step per handler.
using ParseFn = std::function<Instruction(int line, const std::string &mnemonic,
                                          const std::vector<std::string> &ops)>;

static ParseFn makeParser(size_t minOps, size_t maxOps, bool hasMemOperand = false) {
    return [=](int line, const std::string &mn, const std::vector<std::string> &ops) {
        if (ops.size() < minOps || ops.size() > maxOps) {
            throw std::invalid_argument(mn + " expects " +
                                        (minOps == maxOps ? std::to_string(minOps)
                                                          : std::to_string(minOps) + "-" + std::to_string(maxOps)) +
                                        " operand(s), got " + std::to_string(ops.size()));
        }
        if (hasMemOperand) {
            if (ops.size() < 2 || ops[1].empty() || ops[1][0] != '[')
                throw std::invalid_argument(mn + " expects a memory operand like [SP, 0x08]");
            describeMemoryOperand(ops[1]);  // validates syntax
        }
        return Instruction{line, mn, ops};
    };
}

static const std::map<std::string, ParseFn> &handlers() {
    static const std::map<std::string, ParseFn> table = {
        {"SUB", makeParser(3, 3)},
        {"EOR", makeParser(3, 3)},
        {"ADD", makeParser(3, 3)},
        {"AND", makeParser(3, 3)},
        {"MUL", makeParser(3, 3)},
        {"MOV", makeParser(2, 2)},
        {"STR", makeParser(2, 3, true)},
        {"STRB", makeParser(2, 3, true)},
        {"LDR", makeParser(2, 3, true)},
        {"LDRB", makeParser(2, 3, true)},
        {"NOP", makeParser(0, 0)},
        {"B", makeParser(1, 1)},
        {"B.GT", makeParser(1, 1)},
        {"B.LE", makeParser(1, 1)},
        {"CMP", makeParser(2, 2)},
        {"RET", makeParser(0, 1)},
    };
    return table;
}

// Strips comments (//, ;, @) and surrounding whitespace.
static std::string cleanLine(std::string line) {
    for (const char *marker : {"//", ";", "@"}) {
        size_t p = line.find(marker);
        if (p != std::string::npos) line.erase(p);
    }
    return trim(line);
}

// Parses one cleaned line. Throws std::invalid_argument on a bad line.
static Instruction parseLine(int lineNumber, const std::string &line) {
    size_t sp = line.find_first_of(" \t");
    std::string mnemonic = upper(sp == std::string::npos ? line : line.substr(0, sp));
    std::string rest = sp == std::string::npos ? "" : trim(line.substr(sp));

    auto it = handlers().find(mnemonic);
    if (it == handlers().end()) throw std::invalid_argument("unsupported instruction: " + mnemonic);

    return it->second(lineNumber, mnemonic, splitOperands(rest));
}

static void printInstruction(const Instruction &ins) {
    printBanner("Instruction #" + std::to_string(ins.lineNumber) + ":");
    std::printf("Instruction: %s\n", ins.mnemonic.c_str());
    for (size_t i = 0; i < ins.operands.size(); i++) {
        std::string mem = describeMemoryOperand(ins.operands[i]);
        std::printf("Operand #%zu: %s", i + 1, ins.operands[i].c_str());
        if (!mem.empty()) std::printf(" --> %s", mem.c_str());
        std::printf("\n");
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Usage: %s <input.asm>\n", argv[0]);
        return 1;
    }
    std::ifstream in(argv[1]);
    if (!in) {
        std::fprintf(stderr, "Error: cannot open '%s'\n", argv[1]);
        return 1;
    }

    Registers regs;
    Stack stack(0x0);
    regs.sp = stack.base();  // Task 3: stack base goes in SP before the first instruction

    // Task 1: parse each line and show mnemonic + operands.
    std::string raw;
    int lineNumber = 0;
    int errors = 0;
    while (std::getline(in, raw)) {
        lineNumber++;
        std::string line = cleanLine(raw);
        if (line.empty()) continue;
        if (line.back() == ':') continue;  // label-only line (used for branching in later tasks)
        try {
            printInstruction(parseLine(lineNumber, line));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "Error on line %d: %s\n", lineNumber, e.what());
            errors++;
        }
    }

    // Tasks 2 and 3: show the machine state.
    regs.print();
    stack.print();

    return errors ? 2 : 0;
}