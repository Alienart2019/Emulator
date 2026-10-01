#include "parser.h"

#include <cstdio>
#include <fstream>
#include <functional>
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

bool loadProgram(const string &path, Program &prog, vector<string> &errors) {
    ifstream in(path);
    if (!in) {
        errors.push_back("cannot open '" + path + "'");
        return false;
    }

    string raw;
    int lineNumber = 0;
    while (getline(in, raw)) {
        lineNumber++;
        string line = cleanLine(raw);

        // Peel off any leading "label:" definitions (a label may share a line with an instruction).
        while (true) {
            size_t colon = line.find(':');
            if (colon == string::npos) break;
            string label = trim(line.substr(0, colon));
            if (label.empty() || label.find_first_of(" \t[],") != string::npos) break;
            if (prog.labels.count(label))
                errors.push_back("line " + to_string(lineNumber) + ": duplicate label '" + label + "'");
            prog.labels[label] = prog.instructions.size() * 4;
            line = trim(line.substr(colon + 1));
        }
        if (line.empty()) continue;

        try {
            Instruction ins = parseLine(lineNumber, line);
            ins.address = prog.instructions.size() * 4;  // Task 4: 4 bytes per instruction
            prog.instructions.push_back(ins);
        } catch (const exception &e) {
            errors.push_back("line " + to_string(lineNumber) + ": " + e.what());
        }
    }

    // Every branch target must be a known label or a numeric address.
    for (const Instruction &ins : prog.instructions) {
        if (!isBranchMnemonic(ins.mnemonic)) continue;
        const string &target = ins.operands.back();
        uint64_t unused;
        if (!prog.labels.count(target) && !parseImmediate(target, unused))
            errors.push_back("line " + to_string(ins.lineNumber) + ": unknown label '" + target + "'");
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
