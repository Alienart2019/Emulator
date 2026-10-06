#include "parser.h"

#include <cstdio>
#include <fstream>
#include <functional>
#include <regex>
#include <stdexcept>

#include "util.h"

using namespace std;

// ---------------------------------------------------------------------------
// Memory operand description
// ---------------------------------------------------------------------------
string describeMemoryOperand(const string &op) {
    if (op.empty() || op[0] != '[') return "";
    size_t close = op.find(']');
    if (close == string::npos) throw invalid_argument("unterminated '[' in operand: " + op);

    vector<string> parts = splitOperands(op.substr(1, close - 1));
    if (parts.empty()) throw invalid_argument("empty memory operand");

    string desc = parts[0];
    if (parts.size() >= 2) {
        string off = parts[1];
        if (!off.empty() && off[0] == '#') off = off.substr(1);
        if (!off.empty() && off[0] == '-') desc += " - " + trim(off.substr(1));
        else desc += " + " + off;
    }
    for (size_t i = 2; i < parts.size(); i++) desc += " (" + parts[i] + ")";  // e.g. LSL 3

    if (trim(op.substr(close + 1)) == "!") desc += " (pre-index, writeback)";
    return desc;
}

// ---------------------------------------------------------------------------
// Per-instruction parsers
// ---------------------------------------------------------------------------
// Each supported mnemonic has its own parser that checks the operand count and
// (for loads/stores) the memory-operand syntax.
using ParseFn = function<Instruction(int line, const string &mnemonic, const vector<string> &ops)>;

static ParseFn makeParser(size_t minOps, size_t maxOps, bool hasMemOperand = false) {
    return [=](int line, const string &mn, const vector<string> &ops) {
        if (ops.size() < minOps || ops.size() > maxOps) {
            string expected = (minOps == maxOps) ? to_string(minOps)
                                                 : to_string(minOps) + "-" + to_string(maxOps);
            throw invalid_argument(mn + " expects " + expected + " operand(s), got " + to_string(ops.size()));
        }
        if (hasMemOperand) {
            if (ops.size() < 2 || ops[1].empty() || ops[1][0] != '[')
                throw invalid_argument(mn + " expects a memory operand like [SP, 0x08]");
            describeMemoryOperand(ops[1]);  // validates syntax
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
        // required by the assignment
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
        // extras
        {"B.EQ", makeParser(1, 1)},
        {"B.NE", makeParser(1, 1)},
        {"B.GE", makeParser(1, 1)},
        {"B.LT", makeParser(1, 1)},
        {"CBZ", makeParser(2, 2)},
        {"CBNZ", makeParser(2, 2)},
    };
    return table;
}

static bool isBranchMnemonic(const string &m) {
    return m == "B" || m.rfind("B.", 0) == 0 || m == "CBZ" || m == "CBNZ";
}

// ---------------------------------------------------------------------------
// Line parsing
// ---------------------------------------------------------------------------
// Strips comments (//, ;, @) and surrounding whitespace.
static string cleanLine(string line) {
    for (const char *marker : {"//", ";", "@"}) {
        size_t p = line.find(marker);
        if (p != string::npos) line.erase(p);
    }
    return trim(line);
}

Instruction parseLine(int lineNumber, const string &line) {
    size_t sp = line.find_first_of(" \t");
    string mnemonic = upper(sp == string::npos ? line : line.substr(0, sp));
    string rest = (sp == string::npos) ? "" : trim(line.substr(sp));

    auto it = handlers().find(mnemonic);
    if (it == handlers().end()) throw invalid_argument("unsupported instruction: " + mnemonic);

    return it->second(lineNumber, mnemonic, splitOperands(rest));
}

static bool isHexDigits(const string &s) {
    if (s.empty()) return false;
    for (unsigned char c : s)
        if (!isxdigit(c)) return false;
    return true;
}

bool loadProgram(const string &path, Program &prog, vector<string> &errors) {
    ifstream in(path);
    if (!in) {
        errors.push_back("cannot open '" + path + "'");
        return false;
    }

    // objdump:  "   14:<tab>f94007e0 <tab>ldr<tab>x0, [sp, #8]"
    static const regex objdumpInstr(R"(^([0-9a-fA-F]+):\s+([0-9a-fA-F]{8})\s+(.+)$)");
    // objdump:  "0000000000000000 <main>:"
    static const regex objdumpLabel(R"(^([0-9a-fA-F]+)\s+<([^>]+)>:$)");

    string raw;
    int lineNumber = 0;
    uint64_t nextAddr = 0;  // address given to the next plain-assembly instruction
    while (getline(in, raw)) {
        lineNumber++;

        // objdump banner lines: "main.o:     file format elf64-littleaarch64", "Disassembly of section .text:"
        if (raw.find("file format") != string::npos || raw.rfind("Disassembly of section", 0) == 0) continue;

        string line = cleanLine(raw);
        uint64_t address = nextAddr;
        bool fromObjdump = false;
        smatch m;

        if (regex_match(line, m, objdumpLabel)) {
            prog.labels[m[2].str()] = stoull(m[1].str(), nullptr, 16);
            continue;
        }
        if (regex_match(line, m, objdumpInstr)) {
            address = stoull(m[1].str(), nullptr, 16);
            line = trim(m[3].str());
            fromObjdump = true;
        }

        // Peel off any leading "label:" definitions (plain assembly only; a label may share a line with an instruction).
        while (!fromObjdump) {
            size_t colon = line.find(':');
            if (colon == string::npos) break;
            string label = trim(line.substr(0, colon));
            if (label.empty() || label.find_first_of(" \t[],") != string::npos) break;
            if (prog.labels.count(label))
                errors.push_back("line " + to_string(lineNumber) + ": duplicate label '" + label + "'");
            prog.labels[label] = nextAddr;
            line = trim(line.substr(colon + 1));
        }
        if (line.empty()) continue;

        try {
            if (address % 4 != 0) throw invalid_argument("instruction address " + hexStr(address, 1) + " is not a multiple of 4");
            if (prog.addrIndex.count(address)) throw invalid_argument("two instructions at address " + hexStr(address, 1));

            Instruction ins = parseLine(lineNumber, line);
            ins.address = address;  // Task 4: every instruction is 4 bytes
            prog.addrIndex[address] = prog.instructions.size();
            prog.instructions.push_back(ins);
            nextAddr = address + 4;
        } catch (const exception &e) {
            errors.push_back("line " + to_string(lineNumber) + ": " + e.what());
        }
    }

    // Resolve branch targets. A target is a label, or a hex address as objdump prints it
    // ("34 <main+0x34>", "0x34"). Numeric targets are rewritten to "0x34" so they print clearly.
    for (Instruction &ins : prog.instructions) {
        if (!isBranchMnemonic(ins.mnemonic)) continue;
        string &target = ins.operands.back();

        size_t lt = target.find('<');  // drop the "<main+0x34>" symbol hint
        if (lt != string::npos) target = trim(target.substr(0, lt));
        if (prog.labels.count(target)) continue;

        bool hasPrefix = target.size() > 2 && target[0] == '0' && (target[1] == 'x' || target[1] == 'X');
        string digits = hasPrefix ? target.substr(2) : target;
        string where = "line " + to_string(ins.lineNumber) + ": ";
        if (!isHexDigits(digits) || digits.size() > 16) {
            errors.push_back(where + "unknown label '" + target + "'");
            continue;
        }
        uint64_t addr = stoull(digits, nullptr, 16);
        if (!prog.addrIndex.count(addr) && addr != prog.endAddress())
            errors.push_back(where + "branch target " + hexStr(addr, 1) + " is not the address of an instruction");
        target = hexStr(addr, 1);
    }

    return errors.empty();
}

void printInstruction(const Instruction &ins) {
    printBanner("Instruction #" + to_string(ins.lineNumber) + ":");
    printf("Instruction: %s\n", ins.mnemonic.c_str());
    for (size_t i = 0; i < ins.operands.size(); i++) {
        string mem = describeMemoryOperand(ins.operands[i]);
        printf("Operand #%zu: %s", i + 1, ins.operands[i].c_str());
        if (!mem.empty()) printf(" --> %s", mem.c_str());
        printf("\n");
    }
}
