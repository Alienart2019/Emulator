// pre-index, post-index, register offset with LSL, SP at top, forward branch, CBZ
MOV X1, 0xAA
SUB SP, SP, 0x20
STR X1, [SP, 8]!      // SP = top-0x18, store there
MOV X2, 1
MOV X3, 0xBB
STR X3, [SP, X2, LSL 3]   // SP + 8
LDR X4, [SP], 8           // load from SP, then SP += 8
MOV X5, 0
CBZ X5, skip
MOV X6, 0xDEAD            // must be skipped
skip:
CBNZ X5, never
MOV X7, 1
never:
RET
