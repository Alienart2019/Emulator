// parser.h - Task 1 (parser) and Task 4 (instruction addresses / labels)
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Instruction {
    int lineNumber = 0;          // line in the source file (1-based)
    std::uint64_t address = 0;   // Task 4: 0x0, 0x4, 0x8, ...
    std::string mnemonic;        // upper-cased
    std::vector<std::string> operands;
};

struct Program {
    std::vector<Instruction> instructions;          // in file order
    std::map<std::string, std::uint64_t> labels;    // label -> address
    std::map<std::uint64_t, std::size_t> addrIndex; // address -> index into `instructions`

    // Address just past the last instruction (0 for an empty program).
    std::uint64_t endAddress() const {
        return instructions.empty() ? 0 : instructions.back().address + 4;
    }
};

// Describes a memory operand: "[SP, 0x08]" -> "SP + 0x08". Returns "" if not a memory operand.
std::string describeMemoryOperand(const std::string &operand);

// Parses a single (comment-free) line of assembly. Throws std::invalid_argument on a bad line.
Instruction parseLine(int lineNumber, const std::string &line);

// Reads a whole assembly file. Two input styles are accepted, even mixed in one file:
//   1. plain assembly:   "ADD X1, X2, X3"   ("label:" lines allowed; addresses are 0x0, 0x4, ...)
//   2. objdump output:   "  14:\tf94007e0 \tldr\tx0, [sp, #8]"   (address comes from the line;
//                         "<main>:" headers, "file format" lines and "<sym+0x..>" suffixes are handled)
// Branch targets that are not labels are hexadecimal addresses ("b.gt 14 <main+0x14>" or "b 0x14").
// Returns false (and fills `errors`) if the file can't be read or any line is invalid.
bool loadProgram(const std::string &path, Program &program, std::vector<std::string> &errors);

// Prints the "Instruction #N / Operand #k" block from the assignment.
void printInstruction(const Instruction &ins);
