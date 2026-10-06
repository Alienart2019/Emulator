# ARM64 Emulator (CSCI 4448/6659 Project 1)

    make
    ./emulator tests/objdump/test4/test4.txt        # run a program (prints each instruction, then registers + stack)
    ./emulator -q tests/objdump/test4/test4.txt     # quiet: only the final registers and stack
    python3 tests/run_tests.py                      # run all 5 objdump tests and check the results

Input is either plain assembly or `objdump -d` output (see `tests/objdump`). Branch targets that are not
labels are hex addresses, as objdump prints them (`b.gt 14 <main+0x14>`).

| File | Task |
|---|---|
| `parser.*` | 1 (parser), 4 (instruction addresses, labels) |
| `registers.*` | 2 (registers), 6 (32-bit Wn views) |
| `stack.*` | 3 (256-byte stack) |
| `emulator.*` | 4 (PC / branching), 5 (instructions) |
| `main.cpp`, `util.*` | command line, helpers |

SP starts at the top of the stack (base + 256) because compiled code begins with `sub sp, sp, #N`.
Use `--sp-base` to start at the base instead. Run `./emulator --help` for all options.
