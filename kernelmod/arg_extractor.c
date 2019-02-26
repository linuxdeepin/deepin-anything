#include <linux/kernel.h>
#include <linux/module.h>

#ifdef CONFIG_SW
#include <uapi/linux/ptrace.h>
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

#elif defined(CONFIG_CPU_LOONGSON3)

		case 1:  // a0
		case 2:  // a1
		case 3:  // a2
		case 4:  // a3
			return *(unsigned long*)((char *)regs + (3+n)*8);

#elif defined(CONFIG_SW)

		case 1:	return regs->r16;
		case 2: return regs->r17;
		case 3: return regs->r18;
		case 4: return regs->r19;

#elif defined(CONFIG_ARM64)

		case 1: return regs->x0;
		case 2: return regs->x1;
		case 3: return regs->x2;
		case 4: return regs->x3;
		case 5: return regs->x4;
		case 6: return regs->x5;

#endif // CONFIG_X86_64
		default:
			return 0;
	}
	return 0;
}
