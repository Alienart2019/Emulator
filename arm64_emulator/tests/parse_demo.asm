ADD X1, X2, X3
LDR X0, [SP, 0x08]
STRB W1, [SP]
SUB X4, X5, 0x10   // comment
loop:
CMP X1, X2
B.GT loop
LDR X0, [X1, -8]
RET
