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
    std::vector<Instruction> instructions;          // index = address / 4
    std::map<std::string, std::uint64_t> labels;    // label -> address
};

// Describes a memory operand: "[SP, 0x08]" -> "SP + 0x08". Returns "" if not a memory operand.
std::string describeMemoryOperand(const std::string &operand);

// Parses a single (comment-free) line of assembly. Throws std::invalid_argument on a bad line.
Instruction parseLine(int lineNumber, const std::string &line);

// Reads a whole assembly file. Assigns addresses and records labels.
// Returns false (and fills `errors`) if the file can't be read or any line is invalid.
bool loadProgram(const std::string &path, Program &program, std::vector<std::string> &errors);

// Prints the "Instruction #N / Operand #k" block from the assignment.
void printInstruction(const Instruction &ins);
