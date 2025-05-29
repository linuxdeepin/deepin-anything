// Copyright (C) 2021 UOS Technology Co., Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/kernel.h>
#include <linux/module.h>

#ifdef CONFIG_SW
#include <uapi/linux/ptrace.h>
#endif

#if defined(__powerpc64__)
#ifndef CONFIG_PPC64LE
#define CONFIG_PPC64LE 1
#endif
#endif

#if defined(__s390x__)
#ifndef CONFIG_S390X
#define CONFIG_S390X 1
#endif
#endif

unsigned long get_arg(struct pt_regs* regs, int n)
{
/*
	x86_64 args order: rdi, rsi, rdx, rcx, r8, r9, then comes the stack
	loongson3 64bit args order: a0, a1, a2, a3, which is regs[4] ~ regs[7], then comes the stack
	PT_R4(32) is the offset of regs[4] for loongson 64-bit, quoted from asm-offsets.h
*/
	switch (n) {
#if	defined(CONFIG_X86_64)

		case 1:	return regs->di;
		case 2: return regs->si;
		case 3: return regs->dx;
		case 4: return regs->cx;
		case 5: return regs->r8;
		case 6: return regs->r9;

#elif defined(CONFIG_CPU_LOONGSON3) || defined(CONFIG_CPU_LOONGSON64) || defined(CONFIG_LOONGARCH)

		case 1:  // a0
		case 2:  // a1
		case 3:  // a2
		case 4:  // a3
			return *(unsigned long*)((char *)regs + (3+n)*8);

#elif defined(CONFIG_SW)

		case 1: return regs->r16;
		case 2: return regs->r17;
		case 3: return regs->r18;
		case 4: return regs->r19;

#elif defined(CONFIG_SW64)

		case 1: return regs->regs[16];
		case 2: return regs->regs[17];
		case 3: return regs->regs[18];
		case 4: return regs->regs[19];

#elif defined(CONFIG_ARM64) || defined (CONFIG_AARCH64)

		/*修改ARM上anything不能使用的问题,寄存器地址不正确*/
		case 1: return regs->regs[0];
		case 2: return regs->regs[1];
		case 3: return regs->regs[2];
		case 4: return regs->regs[3];

#elif defined(CONFIG_S390X)

		/* s390x argument registers: r2, r3, r4, r5 */
		case 1: return regs->gprs[2];
		case 2: return regs->gprs[3];
		case 3: return regs->gprs[4];
		case 4: return regs->gprs[5];

#elif defined(CONFIG_PPC64LE)

		/* PPC64LE argument registers: r3, r4, r5, r6, r7, r8, r9, r10 */
		case 1: return regs->gpr[3];
		case 2: return regs->gpr[4];
		case 3: return regs->gpr[5];
		case 4: return regs->gpr[6];
		
#elif defined(CONFIG_RISCV)

		/* RISC-V argument registers: a0, a1, a2, a3, a4, a5, a6, a7 */
		case 1: return regs->a0;
		case 2: return regs->a1;
		case 3: return regs->a2;
		case 4: return regs->a3;
		case 5: return regs->a4;
		case 6: return regs->a5;
		case 7: return regs->a6;
		case 8: return regs->a7;

#else
		#error "The current architecture is not supported."

#endif // CONFIG_X86_64
		default:
			return 0;
	}
	return 0;
}
