// Sum 5+4+3+2+1 with a loop, then exercise memory and ALU instructions
MOV X0, 5
MOV X1, 0
loop:
ADD X1, X1, X0
SUB X0, X0, 1
CMP X0, 0
B.GT loop
STR X1, [SP, 0x10]
LDR X2, [SP, 0x10]
STRB X1, [SP, 0x20]
LDRB X3, [SP, 0x20]
MOV X4, 0x1234
MUL X5, X4, X2
EOR X6, X5, X4
AND X7, X6, 0xFF
RET
