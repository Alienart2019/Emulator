// 32-bit (Wn) behavior: reads use low 32 bits, writes zero-extend
MOV X0, -1
MOV W1, W0
ADD W2, W1, 1
MOV X3, 0x123456789
ADD W4, W3, 0
MOV X5, -1
ADD W5, W5, 0
STR W0, [SP]
LDR W6, [SP]
CMP W1, 0
RET
