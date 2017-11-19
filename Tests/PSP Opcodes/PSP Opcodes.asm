.psp
.create "output.bin", 0

				; PSP opcodes

ll	a1,4(a2)
lwc1	f1,(a2)

lv.s	S123,0x20(s0)
lv.s	S321,(s0)

ulv.q	C220,0x40(s1)
ulv.q	C222,0x40(s1)

lvl.q	C220,0x40(s1)
lvr.q	C220,0x40(s1)

lv.q	C530,0x40(s1)
lv.q	C530,(s1)

sc	a1,4(a2)
swc1	f1,(a2)

sv.s	S123,0x20(s0)
sv.s	S321,(s0)

usv.q	C220,0x40(s1)
usv.q	C222,0x40(s1)

svl.q	C220,0x40(s1)
svr.q	C220,0x40(s1)

sv.q	C530,0x40(s1)
sv.q	C530,(s1)
sv.q	C530,0x40(s1), wb
sv.q	C530,(s1), wb

				; Special

rotr	a1,a2,3h
rotr	a1,3h
rotrv	a1,a2,a3
rotrv	a1,a2
clo	a1,a2
clz	a1,a2
madd	a1,a2
maddu	a1,a2
max	a1,a2,a3
min	a1,a2,a3
msub	a1,a2
msubu	a1,a2


				; VFPU0
vadd.s	S100,S220,S333
vsub.p	R122,C430,C010
vsbn.t	c121,C430,C010
vdiv.q	R122,C430,C010


.close