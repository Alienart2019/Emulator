#include "emulator.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

#include "util.h"

using namespace std;

Emulator::Emulator(Program program, uint64_t stackBase, bool spAtTop)
    : prog_(std::move(program)), stack_(stackBase) {
    // Task 3: the stack's address goes into SP before the first instruction.
    regs_.sp = spAtTop ? stackBase + Stack::SIZE : stackBase;
    registerHandlers();
}

// ---------------------------------------------------------------------------
// Operand helpers
// ---------------------------------------------------------------------------
RegRef Emulator::reg(const string &text) const {
    RegRef r;
    if (!Registers::parse(text, r)) throw runtime_error("expected a register, got '" + text + "'");
    return r;
}

uint64_t Emulator::value(const string &text) const {
    RegRef r;
    if (Registers::parse(text, r)) return regs_.read(r);
    uint64_t imm;
    if (parseImmediate(text, imm)) return imm;
    throw runtime_error("invalid operand '" + text + "'");
}

uint64_t Emulator::target(const string &text) const {
    auto it = prog_.labels.find(text);
    if (it != prog_.labels.end()) return it->second;
    uint64_t addr;
    if (parseImmediate(text, addr)) return addr;
    throw runtime_error("unknown branch target '" + text + "'");
}

void Emulator::checkAccess(uint64_t addr, size_t size) const {
    if (!stack_.inRange(addr, size)) {
        throw runtime_error("memory access of " + to_string(size) + " byte(s) at " + hexStr(addr) +
                            " is outside the stack [" + hexStr(stack_.base()) + ", " +
                            hexStr(stack_.base() + Stack::SIZE) + ")");
    }
}

// Decodes the addressing modes:
//   [Xn]   [Xn, #imm]   [Xn, Xm]   [Xn, Xm, LSL #n]   [Xn, #imm]!   [Xn], #imm
Emulator::MemRef Emulator::resolveMemory(const Instruction &ins) const {
    const string &op = ins.operands[1];
    size_t close = op.find(']');
    if (op.empty() || op[0] != '[' || close == string::npos)
        throw runtime_error("bad memory operand '" + op + "'");

    vector<string> parts = splitOperands(op.substr(1, close - 1));
    if (parts.empty()) throw runtime_error("empty memory operand");

    MemRef m;
    m.base = reg(parts[0]);
    if (m.base.is32) throw runtime_error("base register must be 64-bit, got '" + parts[0] + "'");
    uint64_t baseVal = regs_.read(m.base);

    uint64_t offset = 0;
    if (parts.size() >= 2) {
        RegRef offReg;
        if (Registers::parse(parts[1], offReg)) {
            offset = regs_.read(offReg);
            if (parts.size() >= 3) {
                string shift = upper(parts[2]);
                uint64_t amount;
                if (shift.rfind("LSL", 0) != 0 || !parseImmediate(shift.substr(3), amount) || amount > 63)
                    throw runtime_error("unsupported register offset modifier '" + parts[2] + "'");
                offset <<= amount;
            }
        } else if (!parseImmediate(parts[1], offset)) {
            throw runtime_error("bad offset '" + parts[1] + "'");
        }
    }

    bool preIndex = (trim(op.substr(close + 1)) == "!");

    if (ins.operands.size() == 3) {  // post-index: [Xn], #imm
        uint64_t imm;
        if (preIndex || parts.size() >= 2 || !parseImmediate(ins.operands[2], imm))
            throw runtime_error("bad post-index addressing");
        m.addr = baseVal;
        m.writeback = true;
        m.newBase = baseVal + imm;
        return m;
    }

    m.addr = baseVal + offset;
    if (preIndex) {
        m.writeback = true;
        m.newBase = m.addr;
    }
    return m;
}

// ---------------------------------------------------------------------------
// Task 5: one handler per instruction
// ---------------------------------------------------------------------------
void Emulator::registerHandlers() {
    // ADD / SUB / AND / EOR / MUL: Rd = f(Rn, Rm|imm).
    // Rd's width decides the result width: writing a Wn register keeps the low 32 bits
    // and zeroes the upper 32 (Task 6). The low bits of these operations only depend on
    // the low bits of the inputs, so reading the full source registers is fine.
    auto arith = [this](function<uint64_t(uint64_t, uint64_t)> f) {
        return Handler([this, f](const Instruction &i) {
            RegRef rd = reg(i.operands[0]);
            uint64_t a = value(i.operands[1]);
            uint64_t b = value(i.operands[2]);
            regs_.write(rd, f(a, b));
        });
    };
    handlers_["ADD"] = arith([](uint64_t a, uint64_t b) { return a + b; });
    handlers_["SUB"] = arith([](uint64_t a, uint64_t b) { return a - b; });
    handlers_["AND"] = arith([](uint64_t a, uint64_t b) { return a & b; });
    handlers_["EOR"] = arith([](uint64_t a, uint64_t b) { return a ^ b; });
    handlers_["MUL"] = arith([](uint64_t a, uint64_t b) { return a * b; });

    handlers_["MOV"] = [this](const Instruction &i) {
        regs_.write(reg(i.operands[0]), value(i.operands[1]));
    };

    handlers_["NOP"] = [](const Instruction &) {};

    handlers_["CMP"] = [this](const Instruction &i) {
        RegRef rn = reg(i.operands[0]);
        regs_.setFlagsSub(regs_.read(rn), value(i.operands[1]), rn.is32);
    };

    // Loads and stores. Size comes from the mnemonic (B = 1 byte) or from the
    // register width (Xt = 8 bytes, Wt = 4 bytes).
    auto store = [this](size_t fixedSize) {
        return Handler([this, fixedSize](const Instruction &i) {
            RegRef rt = reg(i.operands[0]);
            size_t size = fixedSize ? fixedSize : (rt.is32 ? 4 : 8);
            MemRef m = resolveMemory(i);
            checkAccess(m.addr, size);
            stack_.write(m.addr, regs_.read(rt), size);
            if (m.writeback) regs_.write(m.base, m.newBase);
        });
    };
    auto load = [this](size_t fixedSize) {
        return Handler([this, fixedSize](const Instruction &i) {
            RegRef rt = reg(i.operands[0]);
            size_t size = fixedSize ? fixedSize : (rt.is32 ? 4 : 8);
            MemRef m = resolveMemory(i);
            checkAccess(m.addr, size);
            regs_.write(rt, stack_.read(m.addr, size));  // zero-extends
            if (m.writeback) regs_.write(m.base, m.newBase);
        });
    };
    handlers_["STR"] = store(0);
    handlers_["STRB"] = store(1);
    handlers_["LDR"] = load(0);
    handlers_["LDRB"] = load(1);

    // Branches (Task 4: they change where PC goes next).
    handlers_["B"] = [this](const Instruction &i) { nextPc_ = target(i.operands[0]); };

    auto condBranch = [this](function<bool()> cond) {
        return Handler([this, cond](const Instruction &i) {
            if (cond()) nextPc_ = target(i.operands[0]);
        });
    };
    handlers_["B.GT"] = condBranch([this] { return !regs_.z && regs_.n == regs_.v; });
    handlers_["B.LE"] = condBranch([this] { return regs_.z || regs_.n != regs_.v; });
    handlers_["B.EQ"] = condBranch([this] { return regs_.z; });
    handlers_["B.NE"] = condBranch([this] { return !regs_.z; });
    handlers_["B.GE"] = condBranch([this] { return regs_.n == regs_.v; });
    handlers_["B.LT"] = condBranch([this] { return regs_.n != regs_.v; });

    handlers_["CBZ"] = [this](const Instruction &i) {
        if (regs_.read(reg(i.operands[0])) == 0) nextPc_ = target(i.operands[1]);
    };
    handlers_["CBNZ"] = [this](const Instruction &i) {
        if (regs_.read(reg(i.operands[0])) != 0) nextPc_ = target(i.operands[1]);
    };

    // RET ends the emulation.
    handlers_["RET"] = [this](const Instruction &) { halted_ = true; };
}

void Emulator::execute(const Instruction &ins) {
    auto it = handlers_.find(ins.mnemonic);
    if (it == handlers_.end()) throw runtime_error("no handler for " + ins.mnemonic);
    it->second(ins);
}

// ---------------------------------------------------------------------------
// Task 4: fetch / execute loop with PC
// ---------------------------------------------------------------------------
bool Emulator::run(const EmulatorOptions &opt) {
    error_.clear();
    halted_ = false;
    regs_.pc = 0x0;  // instructions start at address 0x0, 4 bytes each

    const Instruction *current = nullptr;
    uint64_t steps = 0;
    try {
        while (!halted_) {
            uint64_t pc = regs_.pc;
            if (pc % 4 != 0) throw runtime_error("unaligned PC " + hexStr(pc));

            uint64_t index = pc / 4;
            if (index == prog_.instructions.size()) {
                fprintf(stderr, "Note: reached the end of the program at %s without RET\n", hexStr(pc).c_str());
                break;
            }
            if (index > prog_.instructions.size()) throw runtime_error("PC " + hexStr(pc) + " is outside the program");
            if (++steps > opt.maxSteps) throw runtime_error("exceeded " + to_string(opt.maxSteps) + " steps (infinite loop?)");

            const Instruction &ins = prog_.instructions[index];
            current = &ins;
            if (!opt.quiet) printInstruction(ins);

            nextPc_ = pc + 4;       // default: fall through to the next instruction
            execute(ins);           // branches overwrite nextPc_
            if (!halted_) regs_.pc = nextPc_;   // PC stays on the RET that ended the run

            if (opt.trace) regs_.print();
        }
    } catch (const exception &e) {
        error_ = "Error at PC " + hexStr(regs_.pc) +
                 (current ? " (line " + to_string(current->lineNumber) + ")" : "") + ": " + e.what();
        return false;
    }
    return true;
}
