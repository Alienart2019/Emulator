// main.cpp - command-line driver
#include <cstdio>
#include <cstdlib>
#include <string>

#include "emulator.h"
#include "parser.h"
#include "util.h"

using namespace std;

static void usage(const char *prog) {
    printf("Usage: %s [options] <input.asm>\n"
           "\n"
           "Options:\n"
           "  -p, --parse-only   Tasks 1-3 only: print each instruction's operands, then the\n"
           "                     initial registers and stack (nothing is executed)\n"
           "  -q, --quiet        Don't print each instruction as it executes\n"
           "  -t, --trace        Print the registers after every instruction\n"
           "      --base ADDR    Stack base address (default 0x0)\n"
           "      --sp-top       Start SP at the top of the stack (base + 256) instead of the base\n"
           "      --max-steps N  Stop after N instructions (default 1000000)\n"
           "  -h, --help         Show this help\n",
           prog);
}

int main(int argc, char **argv) {
    EmulatorOptions opts;
    bool parseOnly = false;
    bool spTop = false;
    uint64_t base = 0x0;
    string path;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (a == "-p" || a == "--parse-only") {
            parseOnly = true;
        } else if (a == "-q" || a == "--quiet") {
            opts.quiet = true;
        } else if (a == "-t" || a == "--trace") {
            opts.trace = true;
        } else if (a == "--sp-top") {
            spTop = true;
        } else if (a == "--base" || a == "--max-steps") {
            uint64_t v = 0;
            if (i + 1 >= argc || !parseImmediate(argv[i + 1], v)) {
                fprintf(stderr, "Error: %s needs a numeric argument\n", a.c_str());
                return 1;
            }
            i++;
            (a == "--base" ? base : opts.maxSteps) = v;
        } else if (!a.empty() && a[0] == '-') {
            fprintf(stderr, "Error: unknown option '%s'\n", a.c_str());
            usage(argv[0]);
            return 1;
        } else if (path.empty()) {
            path = a;
        } else {
            fprintf(stderr, "Error: more than one input file given\n");
            return 1;
        }
    }

    if (path.empty()) {
        usage(argv[0]);
        return 1;
    }

    Program program;
    vector<string> errors;
    if (!loadProgram(path, program, errors)) {
        for (const string &e : errors) fprintf(stderr, "Error: %s\n", e.c_str());
        return 2;
    }

    Emulator emu(std::move(program), base, spTop);
    bool ok = true;

    if (parseOnly) {
        for (const Instruction &ins : emu.program().instructions) printInstruction(ins);
    } else {
        ok = emu.run(opts);
        if (!ok) fprintf(stderr, "%s\n", emu.error().c_str());
    }

    emu.registers().print();
    emu.stack().print();
    return ok ? 0 : 3;
}
