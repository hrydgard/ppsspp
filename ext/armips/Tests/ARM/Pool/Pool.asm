.gba
.create "output.bin",0
.if 1==1
ldr r1,=0x123
.pool
.endif
.if 0==1
ldr r1,=0x456
.pool
.endif
.if 1==1
ldr r1,=0x789
.pool
.endif
.close
