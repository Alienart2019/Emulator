// emulator.h - Tasks 4-6: PC handling, instruction execution, 32-bit register support
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "parser.h"
#include "registers.h"
#include "stack.h"

struct EmulatorOptions {
    bool quiet = false;                 // don't print each executed instruction
    bool trace = false;                 // print registers after each instruction
    std::uint64_t maxSteps = 1000000;   // guard against infinite loops
};

class Emulator {
public:
    // Compiled code allocates its frame with "sub sp, sp, #N", so SP must start at the TOP of the
    // stack (base + 256) and grow downward. `spAtTop = false` starts SP at the base instead.
    Emulator(Program program, std::uint64_t stackBase = 0x0, bool spAtTop = true);

    Emulator(const Emulator &) = delete;
    Emulator &operator=(const Emulator &) = delete;

    // Runs from address 0x0 until RET, the end of the program, or an error.
    // Returns false on error; see error().
    bool run(const EmulatorOptions &options);

    const Registers &registers() const { return regs_; }
    const Stack &stack() const { return stack_; }
    const Program &program() const { return prog_; }
    const std::string &error() const { return error_; }

private:
    using Handler = std::function<void(const Instruction &)>;

    struct MemRef {
        std::uint64_t addr = 0;      // effective address of the access
        bool writeback = false;      // pre/post-index: base register is updated afterwards
        RegRef base{RegRef::GPR, 0, false};
        std::uint64_t newBase = 0;
    };

    void registerHandlers();
    void execute(const Instruction &ins);

    RegRef reg(const std::string &text) const;               // operand must be a register
    std::uint64_t value(const std::string &text) const;      // register or immediate
    std::uint64_t target(const std::string &text) const;     // label or numeric address
    MemRef resolveMemory(const Instruction &ins) const;      // decodes [Xn, ...] addressing modes
    void checkAccess(std::uint64_t addr, std::size_t size) const;

    Program prog_;
    Registers regs_;
    Stack stack_;
    std::map<std::string, Handler> handlers_;

    std::uint64_t nextPc_ = 0;   // where PC goes after the current instruction (branches change it)
    bool halted_ = false;        // set by RET
    std::string error_;
};
