/* hprof_entry.s - the performance-monitor exception (0xF00) entry for Gamecube/hprof.c.
 * libogc's vector code saved r0-r5, CR, LR, CTR, XER, SRR0 and SRR1 in the frame and
 * comes here with translation on. Save the other volatile registers, give the
 * interrupted PC to hprof_sample (integer code only: the FPU state is not saved), restore,
 * return. The same frame handling as Gamecube/vm/dsihandler.s. hprof_sample also gets LR. */
#include <ppc-asm.h>
#include <ogc/machine/asm.h>

	.extern hprof_sample
FUNC_START(hprof_entry)
	stwu        sp,-EXCEPTION_FRAME_END(sp)
	stw         r6,GPR6_OFFSET(sp)
	stw         r7,GPR7_OFFSET(sp)
	stw         r8,GPR8_OFFSET(sp)
	stw         r9,GPR9_OFFSET(sp)
	stw         r10,GPR10_OFFSET(sp)
	stw         r11,GPR11_OFFSET(sp)
	stw         r12,GPR12_OFFSET(sp)
	mfmsr       r3
	ori         r3,r3,MSR_RI
	mtmsr       r3
	lwz         r3,SRR0_OFFSET(sp)
	lwz         r4,LR_OFFSET(sp)
	bl          hprof_sample
	lwz         r6,GPR6_OFFSET(sp)
	lwz         r7,GPR7_OFFSET(sp)
	lwz         r8,GPR8_OFFSET(sp)
	lwz         r9,GPR9_OFFSET(sp)
	lwz         r10,GPR10_OFFSET(sp)
	lwz         r11,GPR11_OFFSET(sp)
	lwz         r12,GPR12_OFFSET(sp)
	# clear MSR_RI
	mfmsr       r3
	rlwinm      r3,r3,0,31,29
	mtmsr       r3
	lwz         r3,CR_OFFSET(sp)
	lwz         r4,LR_OFFSET(sp)
	lwz         r5,CTR_OFFSET(sp)
	lwz         r0,XER_OFFSET(sp)
	mtcr        r3
	mtlr        r4
	mtctr       r5
	mtxer       r0
	lwz         r0,GPR0_OFFSET(sp)
	lwz         r5,GPR5_OFFSET(sp)
	lwz         r3,SRR0_OFFSET(sp)
	lwz         r4,SRR1_OFFSET(sp)
	mtsrr0      r3
	mtsrr1      r4
	lwz         r3,GPR3_OFFSET(sp)
	lwz         r4,GPR4_OFFSET(sp)
	lwz         sp,GPR1_OFFSET(sp)
	rfi
FUNC_END(hprof_entry)
