#include "parser.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "util.h"

using namespace std;

string describeMemoryOperand(const string &op) {
    if (op.empty() || op[0] != '[') return "";
    size_t close = op.find(']');
    if (close == string::npos)
        throw invalid_argument("unterminated '[' in operand: " + op);

    vector<string> parts = splitOperands(op.substr(1, close - 1));
    if (parts.empty()) throw invalid_argument("empty memory operand");

    string desc = parts[0];
    if (parts.size() >= 2) {
        string off = parts[1];
        if (!off.empty() && off[0] == '#') off = off.substr(1);
        if (!off.empty() && off[0] == '-') desc += " - " + trim(off.substr(1));
        else desc += " + " + off;
    }
    for (size_t i = 2; i < parts.size(); ++i)
        desc += " (" + parts[i] + ")";
    if (trim(op.substr(close + 1)) == "!")
        desc += " (pre-index, writeback)";
    return desc;
}

using ParseFn = function<Instruction(int, const string &, const vector<string> &)>;

static ParseFn makeParser(size_t minOps, size_t maxOps, bool hasMemOperand = false) {
    return [=](int line, const string &mn, const vector<string> &ops) {
        if (ops.size() < minOps || ops.size() > maxOps) {
            string expected = (minOps == maxOps) ? to_string(minOps)
                : to_string(minOps) + "-" + to_string(maxOps);
            throw invalid_argument(mn + " expects " + expected +
                                   " operand(s), got " + to_string(ops.size()));
        }
        if (hasMemOperand) {
            if (ops.size() < 2 || ops[1].empty() || ops[1][0] != '[')
                throw invalid_argument(mn + " expects a memory operand like [SP, #8]");
            describeMemoryOperand(ops[1]);
        }
        Instruction ins;
        ins.lineNumber = line;
        ins.mnemonic = mn;
        ins.operands = ops;
        return ins;
    };
}

static const map<string, ParseFn> &handlers() {
    static const map<string, ParseFn> table = {
        {"SUB", makeParser(3, 3)}, {"EOR", makeParser(3, 3)},
        {"ADD", makeParser(3, 3)}, {"AND", makeParser(3, 3)},
        {"MUL", makeParser(3, 3)}, {"MOV", makeParser(2, 2)},
        {"STR", makeParser(2, 3, true)}, {"STRB", makeParser(2, 3, true)},
        {"LDR", makeParser(2, 3, true)}, {"LDRB", makeParser(2, 3, true)},
        {"NOP", makeParser(0, 0)}, {"B", makeParser(1, 1)},
        {"B.GT", makeParser(1, 1)}, {"B.LE", makeParser(1, 1)},
        {"CMP", makeParser(2, 2)}, {"RET", makeParser(0, 1)},
        {"B.EQ", makeParser(1, 1)}, {"B.NE", makeParser(1, 1)},
        {"B.GE", makeParser(1, 1)}, {"B.LT", makeParser(1, 1)},
        {"CBZ", makeParser(2, 2)}, {"CBNZ", makeParser(2, 2)}
    };
    return table;
}

static bool isBranchMnemonic(const string &m) {
    return m == "B" || m.rfind("B.", 0) == 0 || m == "CBZ" || m == "CBNZ";
}

static string cleanLine(string line) {
    for (const char *marker : {"//", ";", "@"}) {
        size_t p = line.find(marker);
        if (p != string::npos) line.erase(p);
    }
    return trim(line);
}

static bool isHexInstruction(const string &token) {
    if (token.size() != 8) return false;
    for (unsigned char c : token)
        if (!std::isxdigit(c)) return false;
    return true;
}

static int64_t signExtend(uint64_t value, unsigned bits) {
    uint64_t sign = 1ULL << (bits - 1);
    return static_cast<int64_t>((value ^ sign) - sign);
}

static string gpr(unsigned n, bool is64) {
    if (n == 31) return is64 ? "XZR" : "WZR";
    return string(is64 ? "X" : "W") + to_string(n);
}

static string baseReg(unsigned n) {
    return n == 31 ? "SP" : "X" + to_string(n);
}

static string branchAddress(uint64_t pc, int64_t offset) {
    int64_t destination = static_cast<int64_t>(pc) + offset;
    if (destination < 0)
        throw invalid_argument("branch target before program start");
    return "#" + to_string(destination);
}

static string decodeHexInstruction(const string &line, uint64_t pc) {
    uint32_t op = static_cast<uint32_t>(stoul(line, nullptr, 16));

    // Unconditional branch: B (immediate, signed imm26 * 4).
    if ((op & 0xFC000000u) == 0x14000000u) {
        int64_t offset = signExtend(op & 0x03FFFFFFu, 26) * 4;
        return "B " + branchAddress(pc, offset);
    }

    // Conditional branch: B.EQ, B.NE, B.GE, B.LT, B.GT, B.LE.
    if ((op & 0xFF000010u) == 0x54000000u) {
        string mnemonic;
        switch (op & 15u) {
            case 0x0: mnemonic = "B.EQ"; break;
            case 0x1: mnemonic = "B.NE"; break;
            case 0xA: mnemonic = "B.GE"; break;
            case 0xB: mnemonic = "B.LT"; break;
            case 0xC: mnemonic = "B.GT"; break;
            case 0xD: mnemonic = "B.LE"; break;
            default: throw invalid_argument("unsupported branch condition");
        }
        int64_t offset = signExtend((op >> 5) & 0x7FFFFu, 19) * 4;
        return mnemonic + " " + branchAddress(pc, offset);
    }

    // Byte load/store with an unshifted X-register offset.
    if ((op & 0xFFE0EC00u) == 0x38206800u ||
        (op & 0xFFE0EC00u) == 0x38606800u) {
        bool isLoad = (op & 0x00400000u) != 0;
        unsigned rt = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        unsigned rm = (op >> 16) & 31u;
        return string(isLoad ? "LDRB " : "STRB ") + gpr(rt, false) +
               ", [" + baseReg(rn) + ", " + gpr(rm, true) + "]";
    }

    // STRB/LDRB unsigned immediate (byte offset is not scaled).
    if ((op & 0xFFC00000u) == 0x39000000u ||
        (op & 0xFFC00000u) == 0x39400000u) {
        bool isLoad = (op & 0x00400000u) != 0;
        unsigned rt = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        unsigned imm12 = (op >> 10) & 0xFFFu;
        return string(isLoad ? "LDRB " : "STRB ") + gpr(rt, false) +
               ", [" + baseReg(rn) + ", #" + to_string(imm12) + "]";
    }

    // EOR (shifted register), unshifted W or X operands.
    if ((op & 0x7FE0FC00u) == 0x4A000000u) {
        bool is64 = (op >> 31) & 1u;
        unsigned rd = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        unsigned rm = (op >> 16) & 31u;
        return "EOR " + gpr(rd, is64) + ", " + gpr(rn, is64) +
               ", " + gpr(rm, is64);
    }

    // ADD (shifted register), no shift: ADD Wd/Wn/Wm or Xd/Xn/Xm.
    if ((op & 0x7FE0FC00u) == 0x0B000000u) {
        bool is64 = (op >> 31) & 1u;
        unsigned rd = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        unsigned rm = (op >> 16) & 31u;
        return "ADD " + gpr(rd, is64) + ", " + gpr(rn, is64) +
               ", " + gpr(rm, is64);
    }

    // MUL alias of MADD with Ra = XZR/WZR (no accumulation).
    if ((op & 0x7FE0FC00u) == 0x1B007C00u) {
        bool is64 = (op >> 31) & 1u;
        unsigned rd = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        unsigned rm = (op >> 16) & 31u;
        return "MUL " + gpr(rd, is64) + ", " + gpr(rn, is64) +
               ", " + gpr(rm, is64);
    }

    // AND (immediate): low-order contiguous 1-bit masks only.
    if ((op & 0x7F800000u) == 0x12000000u) {
        bool is64 = (op >> 31) & 1u;
        unsigned N = (op >> 22) & 1u;
        unsigned immr = (op >> 16) & 63u;
        unsigned imms = (op >> 10) & 63u;
        unsigned rn = (op >> 5) & 31u;
        unsigned rd = op & 31u;
        if (immr != 0 || N != 0 || imms >= (is64 ? 63u : 31u))
            throw invalid_argument("AND logical-immediate encoding not supported yet");
        uint64_t mask = (1ULL << (imms + 1)) - 1;
        return "AND " + gpr(rd, is64) + ", " + gpr(rn, is64) +
               ", #" + to_string(mask);
    }

    // ADD/SUB immediate; SUBS with Rd=31 is CMP.
    if ((op & 0x1F000000u) == 0x11000000u) {
        bool is64 = (op >> 31) & 1u;
        bool subtract = (op >> 30) & 1u;
        bool setFlags = (op >> 29) & 1u;
        unsigned shift = (op >> 22) & 3u;
        if (shift > 1) throw invalid_argument("invalid immediate shift");
        uint64_t imm = (op >> 10) & 0xFFFu;
        if (shift == 1) imm <<= 12;
        unsigned rn = (op >> 5) & 31u;
        unsigned rd = op & 31u;
        if (setFlags && subtract && rd == 31u)
            return "CMP " + (rn == 31u ? string("SP") : gpr(rn, is64)) +
                   ", #" + to_string(imm);
        if (setFlags)
            throw invalid_argument("flag-setting ADD/SUB destination not supported yet");
        string dest = rd == 31u ? "SP" : gpr(rd, is64);
        string source = rn == 31u ? "SP" : gpr(rn, is64);
        return string(subtract ? "SUB " : "ADD ") + dest + ", " +
               source + ", #" + to_string(imm);
    }

    // MOVZ immediate.
    if ((op & 0x7F800000u) == 0x52800000u) {
        bool is64 = (op >> 31) & 1u;
        unsigned hw = (op >> 21) & 3u;
        unsigned rd = op & 31u;
        if (!is64 && hw > 1) throw invalid_argument("invalid MOVZ shift");
        uint64_t imm = (op >> 5) & 0xFFFFu;
        imm <<= hw * 16;
        return "MOV " + gpr(rd, is64) + ", #" + to_string(imm);
    }

    // LDR/STR unsigned immediate (32-bit and 64-bit).
    uint32_t memClass = op & 0xFFC00000u;
    if (memClass == 0xF9000000u || memClass == 0xF9400000u ||
        memClass == 0xB9000000u || memClass == 0xB9400000u) {
        bool isLoad = (op >> 22) & 1u;
        bool is64 = (op >> 30) & 1u;
        unsigned rt = op & 31u;
        unsigned rn = (op >> 5) & 31u;
        uint64_t offset = ((op >> 10) & 0xFFFu) * (is64 ? 8 : 4);
        return string(isLoad ? "LDR " : "STR ") + gpr(rt, is64) +
               ", [" + baseReg(rn) + ", #" + to_string(offset) + "]";
    }

    if (op == 0xD65F03C0u) return "RET";
    if (op == 0xD503201Fu) return "NOP";

    throw invalid_argument("unsupported ARM64 instruction: " + line);
}

// Internal parser receives the instruction's actual PC, needed for branches.
static Instruction parseLineAt(int lineNumber, const string &line, uint64_t pc) {
    string decoded = trim(line);
    if (decoded.empty()) throw invalid_argument("empty instruction");

    size_t firstSpace = decoded.find_first_of(" \t");
    string firstToken = firstSpace == string::npos
        ? decoded : decoded.substr(0, firstSpace);
    if (isHexInstruction(firstToken))
        decoded = decodeHexInstruction(firstToken, pc);

    size_t sp = decoded.find_first_of(" \t");
    string mnemonic = upper(sp == string::npos ? decoded : decoded.substr(0, sp));
    string rest = sp == string::npos ? "" : trim(decoded.substr(sp));

    auto it = handlers().find(mnemonic);
    if (it == handlers().end())
        throw invalid_argument("unsupported instruction: " + mnemonic);
    return it->second(lineNumber, mnemonic, splitOperands(rest));
}

// Preserve the original two-argument API declared in parser.h.
Instruction parseLine(int lineNumber, const string &line) {
    return parseLineAt(lineNumber, line, 0);
}

bool loadProgram(const string &path, Program &prog, vector<string> &errors) {
    ifstream in(path);
    if (!in) {
        errors.push_back("cannot open '" + path + "'");
        return false;
    }

    string raw;
    int lineNumber = 0;
    while (getline(in, raw)) {
        ++lineNumber;
        string line = cleanLine(raw);

        while (true) {
            size_t colon = line.find(':');
            if (colon == string::npos) break;
            string label = trim(line.substr(0, colon));
            if (label.empty() || label.find_first_of(" \t[],") != string::npos)
                break;
            if (prog.labels.count(label))
                errors.push_back("line " + to_string(lineNumber) +
                                 ": duplicate label '" + label + "'");
            prog.labels[label] = prog.instructions.size() * 4;
            line = trim(line.substr(colon + 1));
        }
        if (line.empty()) continue;

        try {
            uint64_t pc = prog.instructions.size() * 4;
            Instruction ins = parseLineAt(lineNumber, line, pc);
            ins.address = pc;
            prog.instructions.push_back(ins);
        } catch (const exception &e) {
            errors.push_back("line " + to_string(lineNumber) + ": " + e.what());
        }
    }

    for (const Instruction &ins : prog.instructions) {
        if (!isBranchMnemonic(ins.mnemonic)) continue;
        const string &dest = ins.operands.back();
        uint64_t unused;
        if (!prog.labels.count(dest) && !parseImmediate(dest, unused))
            errors.push_back("line " + to_string(ins.lineNumber) +
                             ": unknown label '" + dest + "'");
    }
    return errors.empty();
}

void printInstruction(const Instruction &ins) {
    printBanner("Instruction #" + to_string(ins.lineNumber) + ":");
    printf("Instruction: %s\n", ins.mnemonic.c_str());
    for (size_t i = 0; i < ins.operands.size(); ++i) {
        string mem = describeMemoryOperand(ins.operands[i]);
        printf("Operand #%zu: %s", i + 1, ins.operands[i].c_str());
        if (!mem.empty()) printf(" --> %s", mem.c_str());
        printf("\n");
    }
}
