// signed compare: INT64_MIN vs 1 -> N=1 and V=1, so N==V and "greater than" is FALSE only if flags are right
MOV X0, 0x8000000000000000
MOV X1, 1
CMP X0, X1       // INT64_MIN - 1 overflows: result positive, V=1, N=0 -> LT is true
B.LT less
MOV X2, 0xBAD
RET
less:
MOV X2, 0x600D
RET
