.rsp
.create "output.bin", 0

; RSP opcodes

mfc0 r8,SP_STATUS
mtc0 r8,SP_STATUS

mfc2 r1,v1[15]
mtc2 r1,v1[15]
cfc2 r1,$2
ctc2 r1,$2

vmulf v1,v1,v1[7w]
vmulu v1,v1,v1[7w]
vrndp v1,v1,v1[7w]
vmulq v1,v1,v1[7w]
vmudl v1,v1,v1[7w]
vmudm v1,v1,v1[7w]
vmudn v1,v1,v1[7w]
vmudh v1,v1,v1[7w]

vmacf v1,v1,v1[7w]
vmacu v1,v1,v1[7w]
vrndn v1,v1,v1[7w]
vmacq v1,v1,v1[7w]
vmadl v1,v1,v1[7w]
vmadm v1,v1,v1[7w]
vmadn v1,v1,v1[7w]
vmadh v1,v1,v1[7w]

vadd v1,v1,v1[7w]
vsub v1,v1,v1[7w]
vsut v1,v1,v1[7w]
vabs v1,v1,v1[7w]
vaddc v1,v1,v1[7w]
vsubc v1,v1,v1[7w]
vaddb v1,v1,v1[7w]
vsubb v1,v1,v1[7w]
vaccb v1,v1,v1[7w]
vsucb v1,v1,v1[7w]
vsad v1,v1,v1[7w]
vsac v1,v1,v1[7w]
vsum v1,v1,v1[7w]
vsar v1,v1,v1[7w]
vacc v1,v1,v1[7w]
vsuc v1,v1,v1[7w]

vlt v1,v1,v1[7w]
veq v1,v1,v1[7w]
vne v1,v1,v1[7w]
vge v1,v1,v1[7w]
vcl v1,v1,v1[7w]
vch v1,v1,v1[7w]
vcr v1,v1,v1[7w]
vmrg v1,v1,v1[7w]

vand v1,v1,v1[7w]
vnand v1,v1,v1[7w]
vor v1,v1,v1[7w]
vnor v1,v1,v1[7w]
vxor v1,v1,v1[7w]
vnxor v1,v1,v1[7w]

vrcp v1[1],v1[7]
vrcpl v1[1],v1[7]
vrcph v1[1],v1[7]
vmov v1[1],v1[7]
vrsq v1[1],v1[7]
vrsql v1[1],v1[7]
vrsqh v1[1],v1[7]
vnop

vextt v1,v1,v1[7w]
vextq v1,v1,v1[7w]
vextn v1,v1,v1[7w]

vinst v1,v1,v1[7w]
vinsq v1,v1,v1[7w]
vinsn v1,v1,v1[7w]
vnull

lbv v1[15],1(r31)
lsv v1[15],2(r31)
llv v1[15],4(r31)
ldv v1[15],8(r31)
lqv v1[15],16(r31)
lrv v1[15],16(r31)
lpv v1[15],8(r31)
luv v1[15],8(r31)
lhv v1[15],16(r31)
lfv v1[15],16(r31)
lwv v1[15],16(r31)
ltv v1[15],16(r31)

sbv v1[15],1(r31)
ssv v1[15],2(r31)
slv v1[15],4(r31)
sdv v1[15],8(r31)
sqv v1[15],16(r31)
srv v1[15],16(r31)
spv v1[15],8(r31)
suv v1[15],8(r31)
shv v1[15],16(r31)
sfv v1[15],16(r31)
swv v1[15],16(r31)
stv v1[15],16(r31)

.close
