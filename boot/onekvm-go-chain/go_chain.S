/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * NanoKVM Go chain loader for the factory AArch32 U-Boot.
 *
 * The factory U-Boot runs in Secure SVC (AArch32 EL3).  Its `go` command
 * enters the A32 code at offset 0, which points core 0's reset vector at the
 * A64 code at offset 0x100 and requests a warm reset into AArch64.  The A64
 * code then passes the same BL31 parameters as the OneKVM Go SPL, with a
 * preloaded Linux Image as BL33 and the DTB in x0.
 *
 * The factory SPL parks core 1 in an A32 pen that branches with `bx` to the
 * mailbox address, but BL31 releases secondaries with an A64 entry point
 * through the same mailbox, as the BootROM pen expects.  Core 0 therefore
 * first sends core 1 through a warm reset into AArch64 and back into the
 * BootROM pen; if core 1 does not answer, core 0 boots on alone.
 *
 * The binary is position independent; it must be loaded 256-byte aligned.
 * The parameter block at 0x40 may be patched with `mw` before `go`.
 * Clean the data cache (`dcache off`) before `go`: a warm reset does not
 * write back dirty lines.
 */

#define RVBARADDR0_L		0x02330008
#define RVBARADDR1_L		0x02330010	/* _H is 4 bytes below */
#define CORE1_MAILBOX_L		0x023400dc	/* _H is 4 bytes above */
#define BOOTROM_BASE		0x01860000
#define BOOTROM_SECONDARY_PEN	0x018600ac	/* wfe; read mailbox; br */
#define COUNTER_FREQUENCY	24000000
#define ARM_BL31_PLAT_PARAM_VAL	0x0f1e2d3c4b5a6978
#define PARAM_BL_PARAMS		0x05
#define PARAM_EP		0x01
#define BL33_IMAGE_ID		5
#define EP_NON_SECURE		0x1
/* SPSR_64(MODE_EL2, MODE_SP_ELX, DISABLE_ALL_EXCEPTIONS), as the Go SD SPL */
#define SPSR_EL2H_DAIF		0x3c9
#define EP_INFO_SIZE		88

	.text
	.globl	_start
_start:
	/* AArch32, little endian */
	.word	0xe28f10f8	/* add  r1, pc, #0xf8   @ r1 = _start + 0x100 */
	.word	0xea000015	/* b    core0_a32 */

	.org	0x40
params:
bl31_entry:	.quad	0x40040000
bl33_entry:	.quad	0x40200000
dtb_addr:	.quad	0x43000000
bl33_spsr:	.word	SPSR_EL2H_DAIF
		.word	0

	/* AArch32; r1 holds the core 0 A64 entry */
core0_a32:
	.word	0xe28f2fa6	/* add  r2, pc, #0x298  @ r2 = core1_a32 */
	.word	0xe59f305c	/* ldr  r3, =CORE1_MAILBOX_L */
	.word	0xe3a00000	/* mov  r0, #0 */
	.word	0xe5830004	/* str  r0, [r3, #4]    @ mailbox high */
	.word	0xe5832000	/* str  r2, [r3]        @ mailbox low */
	.word	0xf57ff04f	/* dsb  sy */
	.word	0xe320f004	/* sev */
	.word	0xe3a04601	/* mov  r4, #0x100000   @ poll budget */
	.word	0xe5930000	/* 1: ldr r0, [r3]      @ core 1 clears it */
	.word	0xe3500000	/* cmp  r0, #0 */
	.word	0x0a000003	/* beq  2f */
	.word	0xe2544001	/* subs r4, r4, #1 */
	.word	0x1afffffa	/* bne  1b */
	.word	0xe3a00000	/* mov  r0, #0          @ no answer: */
	.word	0xe5830000	/* str  r0, [r3]        @ withdraw the stub */
	.word	0xe59f0028	/* 2: ldr r0, =RVBARADDR0_L */
	.word	0xe5801000	/* str  r1, [r0] */
	.word	0xe3a01000	/* mov  r1, #0 */
	.word	0xe5001004	/* str  r1, [r0, #-4]   @ RVBARADDR0_H */
	.word	0xf57ff04f	/* dsb  sy */
	.word	0xf57ff06f	/* isb  sy */
	.word	0xe3a00003	/* mov  r0, #3          @ RMR.AA64 | RMR.RR */
	.word	0xee0c0f50	/* mcr  p15, 0, r0, c12, c0, 2 */
	.word	0xf57ff06f	/* isb  sy */
	.word	0xe320f003	/* 3: wfi */
	.word	0xeafffffd	/* b    3b */
	.word	CORE1_MAILBOX_L
	.word	RVBARADDR0_L

	.org	0x100
a64_entry:
	ldr	x0, =COUNTER_FREQUENCY
	msr	cntfrq_el0, x0

	adr	x0, bl_params
	adr	x1, bl33_node
	adr	x2, bl33_ep
	str	x1, [x0, #8]		/* bl_params.head */
	str	x2, [x1, #16]		/* bl33_node.ep_info */
	ldr	x3, bl33_entry
	str	x3, [x2, #8]		/* ep.pc */
	ldr	w3, bl33_spsr
	str	w3, [x2, #16]		/* ep.spsr */
	ldr	x3, dtb_addr
	str	x3, [x2, #24]		/* ep.args.arg0: Linux DTB */
	dsb	sy

	ldr	x4, bl31_entry
	mov	x1, xzr
	mov	x2, x3			/* hw_config, as the SPL passes */
	ldr	x3, =ARM_BL31_PLAT_PARAM_VAL
	isb
	br	x4
	.ltorg

	.balign	8
bl_params:				/* bl_params_t, VERSION_2 */
	.byte	PARAM_BL_PARAMS, 2
	.hword	16
	.word	0
	.quad	0
bl33_node:				/* bl_params_node_t */
	.word	BL33_IMAGE_ID, 0
	.quad	0, 0, 0
bl33_ep:				/* entry_point_info_t, VERSION_1 */
	.byte	PARAM_EP, 1
	.hword	EP_INFO_SIZE
	.word	EP_NON_SECURE
	.quad	0			/* pc */
	.word	0, 0			/* spsr, padding */
	.quad	0, 0, 0, 0, 0, 0, 0, 0	/* args */
bl33_ep_end:

	.if	bl33_ep_end - bl33_ep - EP_INFO_SIZE
	.error	"entry_point_info_t size mismatch"
	.endif

	/* AArch32, entered by core 1 from the factory SPL pen */
	.org	0x300
core1_a32:
	.word	0xe28f1038	/* add  r1, pc, #0x38   @ r1 = core1_a64 */
	.word	0xe59f0024	/* ldr  r0, =RVBARADDR1_L */
	.word	0xe5801000	/* str  r1, [r0] */
	.word	0xe3a01000	/* mov  r1, #0 */
	.word	0xe5001004	/* str  r1, [r0, #-4]   @ RVBARADDR1_H */
	.word	0xf57ff04f	/* dsb  sy */
	.word	0xf57ff06f	/* isb  sy */
	.word	0xe3a00003	/* mov  r0, #3          @ RMR.AA64 | RMR.RR */
	.word	0xee0c0f50	/* mcr  p15, 0, r0, c12, c0, 2 */
	.word	0xf57ff06f	/* isb  sy */
	.word	0xe320f003	/* 1: wfi */
	.word	0xeafffffd	/* b    1b */
	.word	RVBARADDR1_L

	.org	0x340
core1_a64:
	ldr	x0, =COUNTER_FREQUENCY	/* BL31 does not set it on warm boot */
	msr	cntfrq_el0, x0
	ldr	x1, =RVBARADDR1_L
	ldr	w0, =BOOTROM_BASE	/* this code may be overwritten later */
	str	w0, [x1]
	str	wzr, [x1, #-4]
	ldr	x1, =CORE1_MAILBOX_L
	str	wzr, [x1]		/* tell core 0 to continue */
	dsb	sy
	ldr	x2, =BOOTROM_SECONDARY_PEN
	br	x2
	.ltorg
